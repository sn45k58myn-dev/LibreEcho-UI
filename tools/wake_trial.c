/* Bounded, observation-only wake-word trial against waked's post-AEC audio. */
#define _POSIX_C_SOURCE 200809L

#include "adapter/adapter.h"
#include "adapter/voice_dsp.h"
#include "adapter/voice_stream.h"
#include "adapter/wake_worker.h"
#include "json.h"
#include "wake_model.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int open_stream(void)
{
    static const char request[] = "{\"v\":1,\"id\":1,\"cmd\":\"stream_audio\",\"args\":{}}\n";
    struct sockaddr_un address = { .sun_family = AF_UNIX, .sun_path = LE_ADAPTER_WAKEWORD_SOCK };
    struct timeval timeout = { .tv_sec = 1 };
    char response[1024];
    size_t used = 0;
    long long id;
    int ok;
    int64_t started = monotonic_ms();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    if (started < 0 || setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        send(fd, request, sizeof(request) - 1, MSG_NOSIGNAL) != (ssize_t)(sizeof(request) - 1))
        goto fail;
    /* Leave the first binary header in the socket if it came with the response. */
    while (used + 1 < sizeof(response) && monotonic_ms() - started < 2000) {
        if (read(fd, &response[used], 1) != 1)
            goto fail;
        if (response[used] == '\n') {
            response[used] = '\0';
            if (json_valid_object(response, used) &&
                json_get_int64_top_level(response, "id", &id) == 1 && id == 1 &&
                json_get_top_level_bool(response, used, "ok", &ok) == 1 && ok)
                return fd;
            break;
        }
        ++used;
    }
    errno = EPROTO;
fail:
    close(fd);
    return -1;
}

static void detected(const struct le_wake_event *event, void *unused)
{
    (void)unused;
    printf("detection sample=%" PRIu64 " score=%.4f vad=%.4f\n",
           event->detection_sample, event->score, event->vad_score);
}

static int parse_uint(const char *value, unsigned low, unsigned high, unsigned *result)
{
    char *end;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end || parsed < low || parsed > high)
        return -1;
    *result = (unsigned)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    struct le_wake_worker worker = {0};
    struct le_wake_worker_metrics metrics = {0};
    struct le_voice_stream_frame frame;
    struct le_voice_vad vad;
    struct pollfd pollfd = { .fd = -1, .events = POLLIN };
    unsigned seconds, floor = LE_VOICE_VAD_DEFAULT_FLOOR_RMS, frames = 0, voiced = 0;
    uint64_t next_sample = 0;
    int64_t started, deadline, now;
    int playing;
    float threshold;
    char *end, reason[160];
    const char *invalid = NULL;

    if ((argc != 5 && argc != 6) || parse_uint(argv[2], 5, 120, &seconds) < 0 ||
        (strcmp(argv[3], "music") && strcmp(argv[3], "quiet")) ||
        (argc == 6 && parse_uint(argv[5], 1, LE_VOICE_VAD_MAX_FLOOR_RMS, &floor) < 0)) {
        fputs("usage: wake-trial-arm32 MODEL_DIR SECONDS(5..120) music|quiet THRESHOLD [VAD_FLOOR]\n", stderr);
        return 2;
    }
    threshold = strtof(argv[4], &end);
    if (end == argv[4] || *end || !isfinite(threshold) || threshold <= 0 || threshold >= 1) {
        fputs("threshold must be between 0 and 1 exclusive\n", stderr);
        return 2;
    }
    playing = !strcmp(argv[3], "music");
    setvbuf(stdout, NULL, _IOLBF, 0);
    le_voice_vad_init(&vad);
    if (le_voice_vad_set_floor_rms(&vad, floor) < 0)
        return 2;
    if (le_wake_model_validate_directory(argv[1], reason, sizeof(reason)) < 0) {
        fprintf(stderr, "model rejected: %s\n", reason);
        return 1;
    }
    if (le_wake_worker_start(&worker, argv[1], 1, threshold, detected, NULL) < 0) {
        fputs("wake classifier failed to start\n", stderr);
        return 1;
    }
    pollfd.fd = open_stream();
    started = monotonic_ms();
    deadline = started + (int64_t)seconds * 1000;
    if (pollfd.fd < 0)
        invalid = "stream_unavailable";
    else if (started < 0)
        invalid = "clock";
    else
        printf("ready seconds=%u mode=%s threshold=%.4f vad_floor=%u\n", seconds, argv[3], threshold, floor);
    while (!invalid) {
        struct le_voice_vad_result speech;
        struct le_wake_observation observation;
        int readable, submitted;
        now = monotonic_ms();
        if (now < started) {
            invalid = "clock";
            break;
        }
        if (now >= deadline)
            break;
        readable = poll(&pollfd, 1, deadline - now > 1000 ? 1000 : (int)(deadline - now));
        if (readable < 0 && errno == EINTR)
            continue;
        if (readable == 0 && deadline - now <= 1000)
            break;
        if (readable <= 0 || !(pollfd.revents & POLLIN)) {
            invalid = readable == 0 ? "timeout" : "stream_closed";
            break;
        }
        if (le_voice_stream_read_frame(pollfd.fd, &frame) != 1 ||
            frame.flags || frame.sample_count != LE_VOICE_FRAME_SAMPLES) {
            invalid = "incomplete_or_invalid_frame";
            break;
        }
        if ((frames && frame.first_sample != next_sample) ||
            frame.first_sample > UINT64_MAX - frame.sample_count) {
            invalid = "sample_gap";
            break;
        }
        next_sample = frame.first_sample + frame.sample_count;
        ++frames;
        speech = le_voice_vad_process(&vad, frame.samples, frame.sample_count);
        if (speech.active)
            ++voiced;
        observation.detection_sample = next_sample;
        observation.vad_active = speech.active;
        observation.playback_active = playing;
        observation.vad_score = speech.noise_energy
            ? fminf(1.0f, (float)speech.frame_energy / (float)(speech.noise_energy * 6U)) : 0.0f;
        submitted = le_wake_worker_submit(&worker, frame.samples, frame.sample_count, &observation);
        if (submitted != 0)
            invalid = submitted > 0 ? "worker_drop" : "worker_failed";
    }
    if (pollfd.fd >= 0)
        close(pollfd.fd);
    le_wake_worker_stop(&worker, &metrics); /* drains complete queued blocks before reporting */
    if (!invalid && (metrics.failed || metrics.dropped_blocks || !metrics.scores))
        invalid = "inference_incomplete";
    /* Tolerate scheduler/period alignment at the bounds only (two 10 ms frames). */
    if (!invalid && frames + 2 < seconds * (LE_VOICE_RATE / LE_VOICE_FRAME_SAMPLES))
        invalid = "short_capture";
    printf("summary valid=%d reason=%s frames=%u voiced=%u scores=%" PRIu64
           " detections=%" PRIu64 " dropped=%" PRIu64 " max_score=%.4f max_inference_us=%u\n",
           !invalid, invalid ? invalid : "complete", frames, voiced, metrics.scores,
           metrics.events, metrics.dropped_blocks, metrics.max_score, metrics.max_inference_us);
    return invalid != NULL;
}
