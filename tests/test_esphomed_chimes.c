/* #540: opt-in HA feedback through the production nonblocking adapter queue.
 * Private sockets/files only; no hardware or real audio services. */
#define _POSIX_C_SOURCE 200809L
static char dnd_path[512];
#define LE_DND_PATH dnd_path
#define main esphomed_main
#include "../src/adapter/esphomed.c"
#undef main
#include <assert.h>

static int listener,peer;
static char audio_path[108],status_path[512],config_path[512],missing[108];
static unsigned wakes,ends;
static void event(unsigned id){unsigned char b[8];struct ep_writer w={b,sizeof b,0,0};ep_uint(&w,1,id);voice_event(b,w.length);}
static void calls(void){
 for(unsigned i=0;i<JOBS;i++)if(S.jobs[i].fd>=0)(void)io_tick(&S.jobs[i]);
 for(;;){int fd=accept(listener,NULL,NULL);if(fd<0)break;char b[2048];ssize_t n=recv(fd,b,sizeof b-1,MSG_DONTWAIT);if(n<=0){close(fd);continue;}b[n]=0;
  if(strstr(b,"\"cmd\":\"wake_chirp\""))wakes++;
  else if(strstr(b,"\"cmd\":\"event_sound\"")){assert(strstr(b,"\"event\":\"end\""));ends++;}
  else assert(!"unexpected audio transaction");
  assert(!strstr(b,"preview"));close(fd);
 }
 for(unsigned i=0;i<JOBS;i++)io_close(&S.jobs[i]);
}
static void clear(void){turn_clear("success",0);calls();S.now+=1000;S.last_audio_status=S.now;wakes=ends=0;S.clients[0].tx_n=S.clients[0].tx_sent=0;}
static void dnd(int on){FILE *f=fopen(dnd_path,"w");assert(f);assert(fputs(on?"on\n":"off\n",f)>=0);assert(!fclose(f));}
static void config(const char *field){char b[512];int n=snprintf(b,sizeof b,"{\"hostname\":\"fixture\",\"wifi_mac\":\"02:00:00:00:00:01\",\"voice_pipeline_mode\":\"home-assistant\",\"esphome_noise_key\":\"\"%s}",field);assert(n>0&&(size_t)n<sizeof b);assert(!config_write_atomic(config_path,b,(size_t)n));assert(!load_configuration());}
int main(int argc,char **argv){assert(argc==2);const char *dir=argv[1];memset(&S,0,sizeof S);S.owner=0;S.now=S.last_audio_status=1000;S.turn_limit=120000;S.ha_selected=S.active_wake=1;S.plaintext=1;
 for(unsigned i=0;i<CLIENTS;i++)S.clients[i].fd=-1;for(unsigned i=0;i<JOBS;i++)S.jobs[i].fd=-1;S.wake.fd=S.mic.fd=S.mdns_fd=S.timer_cleanup.fd=-1;esp_playback_init(&S.playback);
 snprintf(audio_path,sizeof audio_path,"%s/audio.sock",dir);snprintf(status_path,sizeof status_path,"%s/status.json",dir);snprintf(config_path,sizeof config_path,"%s/config.json",dir);snprintf(dnd_path,sizeof dnd_path,"%s/dnd",dir);snprintf(missing,sizeof missing,"%s/missing.sock",dir);
 S.audio_path=audio_path;S.status_path=status_path;S.config_path=config_path;S.idme_path=missing;S.timer_path=S.led_path=S.wake_path=S.radio_path=missing;
 struct sockaddr_un a;memset(&a,0,sizeof a);a.sun_family=AF_UNIX;memcpy(a.sun_path,audio_path,strlen(audio_path)+1);listener=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);assert(listener>=0&&!bind(listener,(struct sockaddr*)&a,sizeof a)&&!listen(listener,16));
 int sv[2];assert(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,sv));peer=sv[1];S.clients[0].fd=sv[0];S.clients[0].mode=0;S.clients[0].hello=1;

 /* Absent, malformed and duplicate saved keys all remain silent. */
 const char *invalid[]={"",",\"esphome_voice_chimes\":false",",\"esphome_voice_chimes\":\"true\"",",\"esphome_voice_chimes\":true,\"esphome_voice_chimes\":false"};
 for(unsigned i=0;i<NELEM(invalid);i++){config(invalid[i]);assert(!S.voice_chimes);assert(!voice_start(4000,0));capture_end();calls();assert(!wakes&&!ends);clear();}
 config(",\"esphome_voice_chimes\":true");assert(S.voice_chimes);
 /* Wake/button share voice_start: one wake, then one end for STT/VAD/TTS. */
 assert(!voice_start(4000,0));assert(S.turn==1);calls();assert(wakes==1&&!ends);
 event(10);event(4);event(12);event(8);capture_end();event(2);calls();assert(wakes==1&&ends==1);clear();
 assert(!voice_start(4000,1));calls();assert(wakes==1);capture_end();calls();assert(ends==1);clear();
 /* A continuous HA wake stream is not itself a detected request. */
 S.server_wake=1;assert(!server_listen_start());S.turn=2;calls();assert(!wakes&&!ends);event(10);event(10);calls();assert(wakes==1&&!ends);event(12);event(4);calls();assert(wakes==1&&ends==1);clear();
 assert(!server_listen_start());S.turn=2;event(12);calls();assert(!wakes&&!ends);clear();S.server_wake=0;
 /* DND bypass is never requested, and cues do not gate listening. */
 dnd(1);assert(!voice_start(4000,0));capture_end();calls();assert(!wakes&&!ends);clear();dnd(0);
 /* Cancellation drops a queued, unsent cue; no late chirp after cancelling. */
 assert(!voice_start(4000,0));user_cancel("fixture");calls();assert(!wakes&&!ends);clear();S.cancel_guard=0;
 assert(!voice_start(4000,0));event(0);calls();assert(!S.turn&&!wakes&&!ends);clear();
 /* Cancelling a cue must not cancel an unrelated BLE lease sharing the queue. */
 assert(!job(S.audio_path,"proxy_scan","{}",LE_BLE_JOB_LEASE,0));
 struct io *lease=NULL;for(unsigned i=0;i<JOBS;i++)if(S.jobs[i].kind==LE_BLE_JOB_LEASE&&S.jobs[i].fd>=0)lease=&S.jobs[i];assert(lease);
 assert(!voice_start(4000,0));user_cancel("BLE fixture");assert(lease->fd>=0&&lease->stage==1&&!lease->tx_sent);
 io_close(lease);calls();assert(!wakes&&!ends);clear();S.cancel_guard=0;
 /* Even a saturated job queue cannot refuse capture for a best-effort cue. */
 for(unsigned i=0;i<JOBS;i++)S.jobs[i].stage=1;
 assert(!voice_start(4000,0));assert(S.turn==1);capture_end();assert(S.turn==3);calls();assert(!wakes&&!ends);clear();
 /* An actual error reply is not a failed control/volume job. */
 assert(!voice_start(4000,0));
 for(unsigned i=0;i<JOBS;i++)if(S.jobs[i].fd>=0)(void)io_tick(&S.jobs[i]);
 int bad=accept(listener,NULL,NULL);assert(bad>=0);char request[2048];assert(recv(bad,request,sizeof request,MSG_DONTWAIT)>0);
 const char *error="{\"v\":1,\"id\":1,\"ok\":false,\"error\":\"unavailable\"}\n";assert(send(bad,error,strlen(error),MSG_NOSIGNAL)==(ssize_t)strlen(error));close(bad);
 jobs_tick();assert(S.turn==1&&!S.playing&&S.volume==0);clear();
 /* Adapter failure stays best-effort: capture still starts and ends. */
 close(listener);unlink(audio_path);assert(!voice_start(4000,0));assert(S.turn==1);capture_end();assert(S.turn==3);clear();
 for(unsigned i=0;i<JOBS;i++)io_close(&S.jobs[i]);close(peer);close(S.clients[0].fd);unlink(status_path);unlink(dnd_path);unlink(config_path);char bak[520];snprintf(bak,sizeof bak,"%s.bak",config_path);unlink(bak);esp_playback_close(&S.playback);
 puts("HA chimes: opt-in, duplicate stages, server-wake, continuation, DND, cancellation and adapter failure PASS");return 0;
}
