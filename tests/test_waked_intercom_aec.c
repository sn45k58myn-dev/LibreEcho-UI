/* Test the real subscriber routing, filter and stream framing over Unix sockets. */
#define main waked_program_main
#include "../src/adapter/waked.c"
#undef main
#include <assert.h>

static void expect_frame(int fd, int16_t first)
{
    unsigned char message[24 + 2 * LE_VOICE_AEC_FRAME_SAMPLES];
    assert(recv(fd, message, sizeof(message), MSG_WAITALL) == (ssize_t)sizeof(message));
    assert(!memcmp(message, "LVS1", 4));
    assert((uint16_t)(message[24] | message[25] << 8) == (uint16_t)first);
}

static void routing(void)
{
    int pair[2], other[2], replacement[2];
    int16_t ordinary[LE_VOICE_AEC_FRAME_SAMPLES] = {37};
    int16_t corrected[LE_VOICE_AEC_FRAME_SAMPLES] = {-123};
    struct waked_ipc ipc = {0};
    char path[] = "/tmp/libreecho-aec-target-XXXXXX";
    int target_fd = mkstemp(path);
    FILE *target = fdopen(target_fd, "w");
    assert(target);
    fputs("target_id=radar_puffin_plus\n", target);
    fflush(target);
    assert(!echo2_intercom_profile(path));
    rewind(target);
    fputs("target_id=radar_puffin\n", target);
    fclose(target);
    assert(echo2_intercom_profile(path));
    unlink(path);
    assert(!echo2_intercom_profile(path));
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, other) == 0);
    ipc.intercom_fd = -1;
    for (size_t i = 0; i < MAX_WAKE_SUBSCRIBERS; ++i)
        ipc.subscribers[i] = -1;
    ipc.audio_subscribers[0] = pair[0];
    ipc.audio_subscribers[1] = other[0];
    start_intercom_aec(&ipc, pair[0], "/proc/self/exe", geteuid());
    assert(ipc.intercom_fd < 0);
    ipc.intercom_profile = 1;
    start_intercom_aec(&ipc, pair[0], "/bin/sh", geteuid());
    assert(ipc.intercom_fd < 0);
    publish_audio_frame(&ipc, 0, ordinary, LE_VOICE_AEC_FRAME_SAMPLES, NULL);
    expect_frame(pair[1], 37);
    expect_frame(other[1], 37);
#ifdef __linux__
    start_intercom_aec(&ipc, pair[0], "/proc/self/exe", geteuid());
    assert(ipc.intercom_fd == pair[0]);
    start_intercom_aec(&ipc, other[0], "/proc/self/exe", geteuid());
    assert(ipc.intercom_fd == pair[0]);
    publish_audio_frame(&ipc, 160, ordinary, LE_VOICE_AEC_FRAME_SAMPLES, corrected);
    expect_frame(pair[1], -123);
    expect_frame(other[1], 37);
    close(pair[1]);
    publish_audio_frame(&ipc, 320, ordinary, LE_VOICE_AEC_FRAME_SAMPLES, corrected);
    expect_frame(other[1], 37);
    assert(ipc.intercom_fd == -1 && !ipc.intercom_aec.filter.echo_state);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, replacement) == 0);
    assert(add_audio_subscriber(&ipc, replacement[0]) == 0);
    publish_audio_frame(&ipc, 480, ordinary, LE_VOICE_AEC_FRAME_SAMPLES, NULL);
    expect_frame(replacement[1], 37);
    expect_frame(other[1], 37);
    start_intercom_aec(&ipc, replacement[0], "/proc/self/exe", geteuid());
    assert(ipc.intercom_fd == replacement[0]);
    close(replacement[1]);
    reap_subscribers(&ipc);
    assert(ipc.intercom_fd == -1 && !ipc.intercom_aec.filter.echo_state);
#else
    (void)replacement;
    (void)corrected;
    close_audio_subscriber(&ipc, 0);
    close(pair[1]);
#endif
    close_audio_subscriber(&ipc, 1);
    close(other[1]);
}

static void internal_headroom(void)
{
    int16_t ref[LE_VOICE_AEC_FRAME_SAMPLES] = {0};
    int16_t mic[LE_VOICE_AEC_FRAME_SAMPLES] = {0};
    int16_t out[LE_VOICE_AEC_FRAME_SAMPLES];
    struct le_voice_call_aec call = {0};
    assert(le_voice_call_aec_init(&call) == 0);
    mic[0] = INT16_MIN;
    mic[1] = INT16_MAX;
    le_voice_call_aec_process(&call, mic, ref, out);
    assert(call.gain == 1);
    memset(mic, 0, sizeof(mic));
    for (int i = 0; i < 59; ++i)
        le_voice_call_aec_process(&call, mic, ref, out);
    assert(call.gain == 1);
    le_voice_call_aec_process(&call, mic, ref, out);
    assert(call.gain == LE_CALL_AEC_MAX_GAIN);
    ref[0] = INT16_MIN;
    le_voice_call_aec_process(&call, mic, ref, out);
    memset(ref, 0, sizeof(ref));
    for (int i = 0; i < 2; ++i)
        le_voice_call_aec_process(&call, mic, ref, out);
    assert(call.gain == LE_CALL_AEC_MAX_GAIN);
    le_voice_call_aec_process(&call, mic, ref, out);
    assert(call.gain == 1);
    le_voice_call_aec_reset(&call);
    assert(call.delay_position == 0 && call.gain == LE_CALL_AEC_MAX_GAIN);
    for (unsigned i = 0; i < LE_CALL_AEC_DELAY_SAMPLES; ++i)
        assert(call.reference_delay[i] == 0);
    le_voice_call_aec_destroy(&call);
    assert(call.filter.echo_state == NULL);
}

int main(void)
{
    routing();
    internal_headroom();
    puts("waked_intercom_aec: subscriber isolation, hangup, reopen and fixed-point headroom passed");
    return 0;
}
