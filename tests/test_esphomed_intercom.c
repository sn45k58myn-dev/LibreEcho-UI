#define _POSIX_C_SOURCE 200809L
static char intercom_lease[512], intercom_ack[512];
#define LE_IC_LEASE intercom_lease
#define LE_IC_ACK intercom_ack
#define main esphomed_main
#include "../src/adapter/esphomed.c"
#undef main
#include <assert.h>
int main(int argc,char **argv)
{
    char status[512],missing[512];assert(argc==2);
    snprintf(intercom_lease,sizeof intercom_lease,"%s/call.lease",argv[1]);
    snprintf(intercom_ack,sizeof intercom_ack,"%s/ack.lease",argv[1]);
    snprintf(status,sizeof status,"%s/status.json",argv[1]);
    snprintf(missing,sizeof missing,"%s/absent.sock",argv[1]);
    memset(&S,0,sizeof S);S.now=le_ic_now();S.owner=-1;S.privacy=0;S.last_audio_status=S.now;
    S.status_path=status;S.led_path=S.timer_path=S.audio_path=missing;S.mdns_fd=-1;
    S.mic.fd=S.wake.fd=-1;for(unsigned i=0;i<CLIENTS;i++)S.clients[i].fd=-1;
    for(unsigned i=0;i<JOBS;i++)S.jobs[i].fd=-1;
    esp_playback_init(&S.playback);
    assert(!mic_muted());
    assert(!le_ic_lease_write(intercom_lease,S.now));assert(mic_muted());
    S.turn=1;S.server_turn=1;S.pipeline=1;
    intercom_tick();assert(!S.turn&&!S.server_turn&&!strcmp(S.result,"intercom"));
    assert(le_ic_lease_active(intercom_ack,S.now));
    (void)voice_start(0,LE_WAKE_MODEL_STOCK_PHRASE);assert(!S.turn);
    /* No writer refresh after a crash: elapsed time releases capture without
     * changing any saved or physical microphone mute/privacy preference. */
    S.now+=2100;S.last_audio_status=S.now;intercom_tick();assert(!mic_muted());
    assert(!le_ic_lease_active(intercom_ack,S.now));assert(!S.muted&&!S.local_mute_gate&&!S.privacy);
    for(unsigned i=0;i<JOBS;i++)io_close(&S.jobs[i]);esp_playback_close(&S.playback);
    unlink(intercom_lease);unlink(intercom_ack);unlink(status);
    puts("ESPHome intercom cancels continuous capture, acknowledges hold and releases expired lease: PASS");return 0;
}
