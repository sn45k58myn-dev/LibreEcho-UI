#!/usr/bin/env python3
"""Production-shaped /etc factory vs /data live config, in private mounts only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def private_checks():
    if sorted(p.name for p in Path('/sys').iterdir()) != ['.intercom-config-fixture']:
        raise SystemExit('Refusing config fixture without private /sys')
    factory = Path('/etc/libreecho/web-config.json')
    live = Path('/data/libreecho/config/web-config.json')
    assert factory.is_file() and not factory.is_symlink()
    assert json.loads(factory.read_text())['voice_pipeline_mode'] == 'local'
    assert json.loads(live.read_text())['voice_pipeline_mode'] == 'home-assistant'
    assert not Path(str(factory)+'.intercom').exists()
    assert json.loads(Path(str(live)+'.intercom').read_text())['enabled'] is True
    Path('/tmp/privacy').write_text('0\n')
    subprocess.run(['/tmp/probe'], check=True, timeout=5)
    tools = Path('/tmp/tools');tools.mkdir()
    recorder = tools/'start-stop-daemon'
    recorder.write_text('#!'+sys.executable+'\nimport json,os,sys\nopen(os.environ["CAPTURE"],"w").write(json.dumps(sys.argv[1:]))\n')
    recorder.chmod(0o700)

    def capture(overrides):
        env=dict(os.environ)
        for name in ('CONFIG','LE_CONFIG_PATH','ARGS'):env.pop(name,None)
        env.update(PATH=str(tools)+':'+env['PATH'], DAEMON='/tmp/no-daemon',
                   PIDFILE='/tmp/no.pid',SOCKET='/tmp/intercom.sock',LOGFILE='/tmp/intercom.log',CAPTURE='/tmp/argv')
        env.update(overrides)
        subprocess.run(['sh',str(ROOT/'init/libreecho-intercomd.init'),'start'],env=env,
                       check=True,timeout=5,stdout=subprocess.DEVNULL)
        args=json.loads(Path('/tmp/argv').read_text());return args[args.index('--')+1:]

    args=capture({});selected=args[args.index('--config')+1]
    assert selected==str(live),args
    assert json.loads(Path(selected).read_text())['voice_pipeline_mode']=='home-assistant'
    for env,expected in [({'CONFIG':'/data/owner.json'},'/data/owner.json'),
                         ({'LE_CONFIG_PATH':'/data/session.json'},'/data/session.json'),
                         ({'CONFIG':'/data/owner.json','LE_CONFIG_PATH':'/data/session.json'},'/data/owner.json')]:
        args=capture(env);assert args[args.index('--config')+1]==expected,args
    explicit=['--socket','/tmp/custom.sock','--config','/data/args.json']
    assert capture({'ARGS':' '.join(explicit)})==explicit
    defaults=Path('/etc/default/libreecho-intercomd');defaults.write_text('CONFIG=/data/saved-owner.json\n')
    args=capture({});assert args[args.index('--config')+1]=='/data/saved-owner.json',args
    print('intercom shipped init persistent default and explicit overrides: PASS')


if len(sys.argv)>1 and sys.argv[1]=='--private':
    private_checks()
else:
    with tempfile.TemporaryDirectory(prefix='intercom-config-',dir=os.environ.get('TMPDIR')) as tmp:
        d=Path(tmp)
        for name in ('etc/libreecho','etc/default','data/libreecho/config','sys','tmp'):(d/name).mkdir(parents=True,exist_ok=True)
        (d/'etc/libreecho/web-config.json').write_text('{"voice_pipeline_mode":"local"}\n')
        (d/'data/libreecho/config/web-config.json').write_text('{"voice_pipeline_mode":"home-assistant"}\n')
        (d/'data/libreecho/config/web-config.json.intercom').write_text('{"enabled":true}\n')
        (d/'sys/.intercom-config-fixture').write_text('private\n')
        subprocess.run([os.environ.get('CC','cc'),'-D_POSIX_C_SOURCE=200809L','-std=c99','-O1',
                        '-Wall','-Wextra','-Wpedantic','-Werror','-ffunction-sections','-fdata-sections',
                        '-Wl,--gc-sections','-Isrc','-Isrc/adapter','tests/test_intercom_config.c',
                        'src/intercom_protocol.c','src/config_store.c','src/json.c','-o',str(d/'tmp/probe')],
                       cwd=ROOT,check=True,timeout=60)
        command=['bwrap','--unshare-user','--unshare-net','--unshare-pid','--unshare-ipc','--unshare-uts',
                 '--die-with-parent','--tmpfs','/']
        for entry in sorted(Path('/').iterdir()):
            if entry.name in {'tmp','run','data','sys','etc','proc','dev'}:continue
            command+=['--symlink',os.readlink(entry),str(entry)] if entry.is_symlink() else ['--ro-bind',str(entry),str(entry)]
        command+=['--ro-bind',str(ROOT),'/test-source','--proc','/proc','--dev','/dev','--dir','/run']
        for name in ('tmp','etc','data','sys'):command+=['--bind',str(d/name),'/'+name]
        command+=[sys.executable,'/test-source/tests/test_intercom_config.py','--private']
        subprocess.run(command,check=True,timeout=30)
