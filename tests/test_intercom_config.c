#define _POSIX_C_SOURCE 200809L
#define main intercom_daemon_main
#include "../src/adapter/intercomd.c"
#undef main
#include <assert.h>

/* Only the config/privacy gates execute. There is no microphone, PCM or LED
 * access; the final audio-status gate is a bounded fixture response. */
struct le_adapter { int unused; };
static struct le_adapter fixture;
struct le_adapter *le_adapter_connect(const char *path,int timeout)
{ (void)path;(void)timeout;return &fixture; }
void le_adapter_close(struct le_adapter *a) { (void)a; }
void le_adapter_set_io_timeout(struct le_adapter *a,int timeout)
{ (void)a;(void)timeout; }
int le_adapter_call(struct le_adapter *a,const char *cmd,const char *args,char *out,size_t size)
{
    (void)a;(void)args;assert(!strcmp(cmd,"status"));
    assert(snprintf(out,size,"{\"muted\":false,\"do_not_disturb\":false,\"output_available\":true,\"volume\":50,\"notification_volume\":50}")<(int)size);
    return 0;
}
int main(void)
{
    struct stat factory,live;
    assert(access("/sys/.intercom-config-fixture",R_OK)==0);
    assert(!lstat("/etc/libreecho/web-config.json",&factory)&&S_ISREG(factory.st_mode));
    assert(!lstat("/data/libreecho/config/web-config.json",&live)&&S_ISREG(live.st_mode));
    assert(factory.st_ino!=live.st_ino||factory.st_dev!=live.st_dev);
    privacy="/tmp/privacy";
    assert(permitted()&&"bare companion must read live HA config and live opt-in");
    config="/etc/libreecho/web-config.json";
    assert(!permitted()&&"explicit factory config must remain refused, without falling back");
    puts("intercom native persistent default versus regular factory file: PASS");
    return 0;
}
