/* LAN intercom companion. The existing micd/waked/audiod retain ownership of
 * every PCM device. This process only subscribes to post-AEC audio and writes
 * the existing system bus. No idle microphone subscription, recording or TLS. */
#include "../intercom_protocol.h"
#include "../config_store.h"
#include "../json.h"
#include "adapter.h"
#include "radio_resample.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static const char *config = "/etc/libreecho/web-config.json";
static const char *privacy = "/sys/devices/platform/amz_privacy/privacy_state";
static const char *audio = "/run/libreecho/audio.sock";
static const char *wake = "/run/libreecho/wakeword.sock";
static const char *led = "/run/libreecho/led.sock";
static const char *bus = "/run/libreecho-audio/system.pcm";
static const char *socket_path = LE_IC_SOCKET;
static const char *lease = LE_IC_LEASE, *ack = LE_IC_ACK;
static struct {
    int client, mic, output, stage;
    uint64_t started, checked, indicated, refreshed, last_input, mic_sample;
    unsigned char mic_rx[8192], packet[LE_IC_PCM_BYTES];
    size_t mic_n, packet_n, output_n;
    int16_t output_pcm[2048];
    struct le_radio_resampler resampler;
} s = { .client = -1, .mic = -1, .output = -1 };

static void stop_signal(int sig) { (void)sig; running = 0; }
static int command(const char *path, const char *cmd, const char *args, char *out, size_t size)
{
    struct le_adapter *a = le_adapter_connect(path, 60); int rc;
    if (!a) return -1;
    le_adapter_set_io_timeout(a, 60);
    rc = le_adapter_call(a, cmd, args, out, size); le_adapter_close(a); return rc;
}
static int indicator(void)
{
    /* Finite repeats are renewed: a killed process cannot leave a permanent
     * call light behind. The owner-scoped stop preserves other LED owners. */
    return command(led, "pattern", "{\"name\":\"flash\",\"r\":0,\"g\":180,\"b\":255,\"brightness\":70,\"repeats\":5,\"owner\":\"intercom\"}", NULL, 0);
}
static void end_call(void)
{
    if (s.client >= 0) close(s.client);
    if (s.mic >= 0) close(s.mic);
    if (s.output >= 0) close(s.output);
    unlink(lease); unlink(ack);
    if (s.stage) (void)command(led, "pattern", "{\"name\":\"stop\",\"owner\":\"intercom\"}", NULL, 0);
    memset(&s, 0, sizeof s); s.client = s.mic = s.output = -1;
}
static int permitted(void)
{
    char b[16384], optin[512], mode[32], status[4096];
    int n, dnd = 1, on = 0, muted = 1, output = 0, volume = 0, notification = 0;
    if (!le_ic_optin_path(config, optin, sizeof optin)) return 0;
    n = config_read(optin, b, sizeof b);
    if (n <= 0 || !json_valid_object(b, (size_t)n) || json_duplicate_key(b, (size_t)n, "enabled") ||
        json_get_top_level_bool(b, (size_t)n, "enabled", &on) <= 0 || !on) return 0;
    n = config_read(config, b, sizeof b);
    if (n <= 0 || !json_valid_object(b, (size_t)n) ||
        json_duplicate_key(b, (size_t)n, "voice_pipeline_mode") ||
        json_get_string_top_level(b, "voice_pipeline_mode", mode, sizeof mode) <= 0 ||
        strcmp(mode, "home-assistant")) return 0;
    n = config_read(privacy, b, sizeof b);
    if (n <= 0 || b[0] != '0' || (b[1] && b[1] != '\n')) return 0;
    if (command(audio, "status", "{}", status, sizeof status) ||
        json_get_bool(status, "muted", &muted) <= 0 || muted ||
        json_get_bool(status, "do_not_disturb", &dnd) <= 0 || dnd ||
        json_get_bool(status, "output_available", &output) <= 0 || !output ||
        json_get_int(status, "volume", &volume) <= 0 || volume <= 0 ||
        json_get_int(status, "notification_volume", &notification) <= 0 || notification <= 0) return 0;
    return 1;
}
static int microphone_open(void)
{
    struct sockaddr_un a;
    static const char request[] = "{\"v\":1,\"id\":1,\"cmd\":\"stream_audio\",\"args\":{}}\n";
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0 || strlen(wake) >= sizeof a.sun_path) { if (fd >= 0) close(fd); return -1; }
    memset(&a, 0, sizeof a); a.sun_family = AF_UNIX; strcpy(a.sun_path, wake);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) ||
        send(fd, request, sizeof request - 1, MSG_NOSIGNAL) != sizeof request - 1) {
        close(fd); return -1;
    }
    s.mic = fd; return 0;
}
static unsigned little32(const unsigned char *p)
{ return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }
static uint64_t little64(const unsigned char *p)
{ return (uint64_t)little32(p) | (uint64_t)little32(p + 4) << 32; }
static int microphone_read(void)
{
    ssize_t n = recv(s.mic, s.mic_rx + s.mic_n, sizeof s.mic_rx - s.mic_n, MSG_DONTWAIT);
    unsigned frames = 0;
    if (n <= 0) return n < 0 && (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    s.mic_n += (size_t)n;
    if (s.stage == 2) {
        unsigned char *e = memchr(s.mic_rx, '\n', s.mic_n); size_t used;
        if (!e) return s.mic_n >= 4096 ? -1 : 0;
        used = (size_t)(e - s.mic_rx) + 1; *e = 0;
        if (!strstr((char *)s.mic_rx, "\"streaming\":true") ||
            !strstr((char *)s.mic_rx, "\"sample_indexed\":true")) return -1;
        memmove(s.mic_rx, s.mic_rx + used, s.mic_n - used); s.mic_n -= used;
        s.stage = 3;
        if (send(s.client, "READY", 5, MSG_DONTWAIT | MSG_NOSIGNAL) != 5) return -1;
    }
    while (s.mic_n >= 24 && frames++ < 8) {
        unsigned count; size_t used, i; uint64_t first;
        if (memcmp(s.mic_rx, "LVS1", 4) || s.mic_rx[4] != 1 || s.mic_rx[5] ||
            s.mic_rx[6] || s.mic_rx[7] || little32(s.mic_rx + 20)) return -1;
        count = little32(s.mic_rx + 16); first = little64(s.mic_rx + 8);
        if (!count || count > 1280) return -1;
        used = 24u + count * 2u;
        if (s.mic_n < used) break;
        if (s.mic_sample && first != s.mic_sample) return -1;
        s.mic_sample = first + count;
        for (i = 0; i < count * 2u; i++) {
            s.packet[s.packet_n++] = s.mic_rx[24 + i];
            if (s.packet_n == sizeof s.packet) {
                if (send(s.client, s.packet, sizeof s.packet, MSG_DONTWAIT | MSG_NOSIGNAL) != sizeof s.packet) return -1;
                s.packet_n = 0;
            }
        }
        memmove(s.mic_rx, s.mic_rx + used, s.mic_n - used); s.mic_n -= used;
    }
    return s.mic_n == sizeof s.mic_rx ? -1 : 0;
}
static int phone_read(uint64_t now)
{
    unsigned char b[LE_IC_PCM_BYTES + 1]; int16_t pcm[LE_IC_SAMPLES];
    ssize_t n = recv(s.client, b, sizeof b, MSG_DONTWAIT | MSG_TRUNC); unsigned i; int count;
    if (n <= 0) return n < 0 && (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (s.stage == 0) {
        if (n != 5 || memcmp(b, "LEIC1", 5) || !permitted()) return -1;
        unlink(ack);
        if (le_ic_lease_write(lease, now)) return -1;
        s.stage = 1; s.started = s.last_input = s.refreshed = now; return 0;
    }
    if (s.stage != 3 || n != LE_IC_PCM_BYTES || s.output_n) return -1;
    for (i = 0; i < LE_IC_SAMPLES; i++) pcm[i] = (int16_t)((unsigned)b[i * 2] | (unsigned)b[i * 2 + 1] << 8);
    count = le_radio_resample(&s.resampler, pcm, LE_IC_SAMPLES, 1, 16000, s.output_pcm, 1024);
    if (count <= 0 || count > 1024) return -1;
    s.output_n = (size_t)count * 4u; s.last_input = now; return 0;
}
int main(int argc, char **argv)
{
    int listener, i; struct sockaddr_un a; struct sigaction action;
    for (i = 1; i < argc; i++) {
        const char *v;
        if (i + 1 >= argc) return 2;
        v = argv[++i];
        if (!strcmp(argv[i - 1], "--config")) config = v;
        else if (!strcmp(argv[i - 1], "--privacy-state")) privacy = v;
        else if (!strcmp(argv[i - 1], "--audio-socket")) audio = v;
        else if (!strcmp(argv[i - 1], "--wake-socket")) wake = v;
        else if (!strcmp(argv[i - 1], "--led-socket")) led = v;
        else if (!strcmp(argv[i - 1], "--audio-bus")) bus = v;
        else if (!strcmp(argv[i - 1], "--socket")) socket_path = v;
        else if (!strcmp(argv[i - 1], "--lease")) lease = v;
        else if (!strcmp(argv[i - 1], "--assist-ack")) ack = v;
        else return 2;
    }
    if (strlen(socket_path) >= sizeof a.sun_path) return 2;
    memset(&action, 0, sizeof action); action.sa_handler = stop_signal;
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL); signal(SIGPIPE, SIG_IGN);
    listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener < 0) return 1;
    memset(&a, 0, sizeof a); a.sun_family = AF_UNIX; strcpy(a.sun_path, socket_path);
    unlink(socket_path); umask(0077);
    if (bind(listener, (struct sockaddr *)&a, sizeof a) || chmod(socket_path, 0600) || listen(listener, 1)) return 1;
    unlink(lease); unlink(ack);
    while (running) {
        struct pollfd p[4] = {{listener, POLLIN, 0}, {s.client, POLLIN, 0},
            {s.mic, POLLIN, 0}, {s.output, s.output_n ? POLLOUT : 0, 0}};
        uint64_t now; int rc = poll(p, 4, 10);
        if (rc < 0 && errno != EINTR) break;
        now = le_ic_now();
        if (p[0].revents & POLLIN) {
            int fd = accept(listener, NULL, NULL);
            if (fd >= 0) {
                if (s.client >= 0 || fcntl(fd, F_SETFL, O_NONBLOCK) || fcntl(fd, F_SETFD, FD_CLOEXEC)) close(fd);
                else { s.client = fd; s.started = now; }
            }
        }
        if (s.client < 0) continue;
        if ((p[1].revents & (POLLHUP | POLLERR | POLLNVAL)) ||
            (p[2].revents & (POLLHUP | POLLERR | POLLNVAL)) ||
            (p[3].revents & (POLLHUP | POLLERR | POLLNVAL))) { end_call(); continue; }
        if ((p[1].revents & POLLIN) && phone_read(now)) { end_call(); continue; }
        if (!s.stage) { if (now - s.started > 2000u) end_call(); continue; }
        if (now - s.checked >= 250u) {
            s.checked = now;
            if (!permitted()) { end_call(); continue; }
        }
        if (now - s.started > 600000u || now - s.last_input > 3000u) { end_call(); continue; }
        if (now - s.refreshed >= 250u) {
            s.refreshed = now;
            if (le_ic_lease_write(lease, now)) { end_call(); continue; }
        }
        if (s.stage == 1) {
            if (!le_ic_lease_active(ack, now)) { if (now - s.started > 1500u) end_call(); continue; }
            { char cue[256]; int playing = 1;
              if (command(audio, "wake_chirp", "{}", cue, sizeof cue) ||
                  (json_get_bool(cue, "playing", &playing) > 0 && !playing) || indicator()) { end_call(); continue; } }
            s.indicated = now;
            s.output = open(bus, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
            { struct stat st;
              if (s.output < 0 || fstat(s.output, &st) || !S_ISFIFO(st.st_mode) || microphone_open()) { end_call(); continue; } }
            s.stage = 2;
        }
        if (!le_ic_lease_active(ack, now)) { end_call(); continue; }
        if (now - s.indicated >= 1000u) {
            s.indicated = now;
            if (indicator()) { end_call(); continue; }
        }
        if ((p[2].revents & POLLIN) && microphone_read()) { end_call(); continue; }
        if (s.output_n) {
            ssize_t written = write(s.output, s.output_pcm, s.output_n);
            if (written == (ssize_t)s.output_n) s.output_n = 0;
            else if (written >= 0 || (errno != EAGAIN && errno != EINTR)) { end_call(); continue; }
        }
    }
    end_call(); close(listener); unlink(socket_path); return 0;
}
