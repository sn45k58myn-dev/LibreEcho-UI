#define _POSIX_C_SOURCE 200809L

#include "adapter/voice_aec_guard.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", \
                __FILE__, __LINE__, #x, errno); \
        result = 1; \
        goto out; \
    } \
} while (0)

struct test_packet {
    struct le_voice_reference_header header;
    int16_t samples[LE_VOICE_REFERENCE_MAX_PACKET_FRAMES];
};

/* Fake reference sender: 2048 frames at 48 kHz -> about 682 samples at 16. */
static int send_packet(int fd, const char *path, uint32_t sequence,
                       uint64_t stamp, int active)
{
    struct sockaddr_un address;
    struct test_packet packet;
    size_t bytes;
    size_t i;

    memset(&packet, 0, sizeof(packet));
    packet.header.magic = LE_VOICE_REFERENCE_MAGIC;
    packet.header.version = LE_VOICE_REFERENCE_VERSION;
    packet.header.header_bytes = sizeof(packet.header);
    packet.header.sequence = sequence;
    packet.header.sample_rate = LE_VOICE_REFERENCE_INPUT_RATE;
    packet.header.channels = 1;
    packet.header.frames = LE_VOICE_REFERENCE_MAX_PACKET_FRAMES;
    packet.header.activity_mask = active ? 1U : 0U;
    packet.header.monotonic_ns = stamp;
    for (i = 0; i < packet.header.frames; ++i)
        packet.samples[i] = (int16_t)(((i % 200U) - 100U) * 20);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    bytes = sizeof(packet.header) +
            packet.header.frames * sizeof(packet.samples[0]);
    return sendto(fd, &packet, bytes, 0, (struct sockaddr *)&address,
                  (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                              strlen(path) + 1U)) == (ssize_t)bytes
        ? 0 : -1;
}

/* One 10 ms lock-step frame with a synthetic mic, as waked does it. */
static void frame(struct le_voice_aec *aec,
                  struct le_voice_aec_guard *guard,
                  struct le_voice_reference *reference,
                  uint64_t now_ns, uint64_t *aec_frames)
{
    int16_t mic[LE_VOICE_AEC_FRAME_SAMPLES];
    int16_t far_end[LE_VOICE_AEC_FRAME_SAMPLES];
    int16_t clean[LE_VOICE_AEC_FRAME_SAMPLES];
    size_t i;

    for (i = 0; i < LE_VOICE_AEC_FRAME_SAMPLES; ++i)
        mic[i] = (int16_t)(((i * 7U) % 300U) - 150);
    (void)le_voice_aec_guard_update(guard, aec, reference, now_ns);
    if (le_voice_reference_active(reference, now_ns, LE_VOICE_REFERENCE_TAIL_NS)) {
        le_voice_reference_read(reference, far_end,
                                LE_VOICE_AEC_FRAME_SAMPLES);
        le_voice_aec_process(aec, mic, far_end, clean);
        ++*aec_frames;
    }
}

static unsigned int status_value(const char *path, const char *key)
{
    char line[96];
    size_t klen = strlen(key);
    unsigned int value = 0xFFFFFFFFU;
    FILE *file = fopen(path, "r");

    if (!file)
        return value;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=')
            value = (unsigned int)strtoul(line + klen + 1, NULL, 10);
    }
    fclose(file);
    return value;
}

int main(void)
{
    char root[] = "/tmp/libreecho-aec-guard.XXXXXX";
    char sock[108];
    char status[108];
    char tmp[120];
    struct le_voice_reference reference;
    struct le_voice_aec_guard guard;
    struct le_voice_aec aec;
    uint64_t aec_frames = 0;
    uint64_t now = 5000000000ULL;
    size_t before;
    int sender = -1;
    int result = 0;
    int i;

    memset(&reference, 0, sizeof(reference));
    reference.fd = -1;
    memset(&aec, 0, sizeof(aec));
    /* Default path must be readable by SSH users: /run/libreecho is 0700. */
    CHECK(strcmp(LE_VOICE_AEC_GUARD_STATUS_PATH,
                 "/run/libreecho-audio/waked-aec.status") == 0);
    CHECK(mkdtemp(root) != NULL);
    CHECK(snprintf(sock, sizeof(sock), "%s/ref.sock", root) > 0);
    CHECK(snprintf(status, sizeof(status), "%s/aec.status", root) > 0);
    CHECK(le_voice_reference_open(&reference, sock) == 0);
    CHECK(le_voice_aec_init(&aec, LE_VOICE_AEC_FRAME_SAMPLES,
                            LE_VOICE_AEC_DEFAULT_FILTER_SAMPLES) == 0);
    le_voice_aec_guard_init(&guard, status);
    sender = socket(AF_UNIX, SOCK_DGRAM, 0);
    CHECK(sender >= 0);

    /* Idle keepalives cannot seed a stale queue at the next call. */
    CHECK(send_packet(sender, sock, 0, now, 0) == 0);
    CHECK(send_packet(sender, sock, 1, now, 0) == 0);
    CHECK(le_voice_reference_drain(&reference) == 2);
    CHECK(reference.count == 0);
    frame(&aec, &guard, &reference, now, &aec_frames);
    CHECK(guard.aec_resets == 0);
    CHECK(aec_frames == 0);

    /* A new active period and padding both arrive before the mic wakes.
     * The first period must survive, and padding must not turn AEC off. */
    now += 10000000ULL;
    CHECK(send_packet(sender, sock, 2, now, 1) == 0);
    CHECK(send_packet(sender, sock, 3, now, 0) == 0);
    CHECK(le_voice_reference_drain(&reference) == 2);
    before = reference.count;
    CHECK(before >= 1364);
    frame(&aec, &guard, &reference, now, &aec_frames);
    CHECK(guard.aec_resets == 0 && guard.edge_clears == 1);
    CHECK(guard.discontinuity_resets == 0);
    CHECK(reference.count == before - LE_VOICE_AEC_FRAME_SAMPLES);
    CHECK(reference.underflows == 0 && aec_frames == 1);

    /* Continuous reference: no further resets, ring not cleared. */
    for (i = 4; i < 9; ++i)
        CHECK(send_packet(sender, sock, (uint32_t)i, now, 1) == 0);
    CHECK(le_voice_reference_drain(&reference) == 5);
    before = reference.count;
    CHECK(before > 1000);
    for (i = 0; i < 20; ++i) {
        now += 10000000ULL;
        frame(&aec, &guard, &reference, now, &aec_frames);
    }
    CHECK(guard.aec_resets == 0);
    CHECK(reference.count == before - 20U * LE_VOICE_AEC_FRAME_SAMPLES);
    CHECK(reference.discontinuities == 0);

    /* Sequence gap: reference drops its ring, AEC is reset. */
    CHECK(send_packet(sender, sock, 10, now, 1) == 0);
    CHECK(le_voice_reference_drain(&reference) == 1);
    CHECK(reference.discontinuities == 1);
    CHECK(reference.count < 700);
    now += 10000000ULL;
    frame(&aec, &guard, &reference, now, &aec_frames);
    CHECK(guard.aec_resets == 1 && guard.discontinuity_resets == 1);
    CHECK(guard.edge_clears == 1);
    for (i = 0; i < 5; ++i) {
        now += 10000000ULL;
        frame(&aec, &guard, &reference, now, &aec_frames);
    }
    CHECK(guard.aec_resets == 1);

    /* After a true idle gap preserve the next active packet, even when
     * no separate keepalive or idle microphone frame marks the boundary. */
    now += 500000000ULL;
    CHECK(!le_voice_reference_active(&reference, now, LE_VOICE_REFERENCE_TAIL_NS));
    CHECK(send_packet(sender, sock, 11, now, 1) == 0);
    CHECK(le_voice_reference_drain(&reference) == 1);
    now += 10000000ULL;
    before = reference.count;
    frame(&aec, &guard, &reference, now, &aec_frames);
    CHECK(guard.aec_resets == 1 && guard.edge_clears == 2);
    CHECK(before >= 680 && before <= 683);
    CHECK(reference.count == before - LE_VOICE_AEC_FRAME_SAMPLES);

    /* Status file: counters, rate-limited rewrite, forced rewrite. */
    le_voice_aec_guard_status(&guard, &reference, 1, aec_frames, now, 0);
    CHECK(status_value(status, "aec_resets") == 1);
    CHECK(status_value(status, "ref_edge_clears") == 2);
    CHECK(status_value(status, "aec_discontinuity_resets") == 1);
    CHECK(status_value(status, "ref_discontinuities") == 1);
    CHECK(status_value(status, "ref_packets") == reference.packets);
    CHECK(status_value(status, "ref_underflows") == reference.underflows);
    CHECK(status_value(status, "ref_overruns") == reference.overruns);
    CHECK(status_value(status, "ref_active") == 1);
    CHECK(status_value(status, "aec_frames") == (unsigned int)aec_frames);
    now += 1000000000ULL;
    le_voice_aec_guard_status(&guard, &reference, 0, aec_frames + 5U, now,
                              0);
    CHECK(status_value(status, "aec_frames") == (unsigned int)aec_frames);
    le_voice_aec_guard_status(&guard, &reference, 0, aec_frames + 5U, now,
                              1);
    CHECK(status_value(status, "aec_frames") ==
          (unsigned int)aec_frames + 5U);
    CHECK(status_value(status, "ref_active") == 0);
    now += 10000000000ULL;
    le_voice_aec_guard_status(&guard, &reference, 0, aec_frames + 9U, now,
                              0);
    CHECK(status_value(status, "aec_frames") ==
          (unsigned int)aec_frames + 9U);
    puts("voice_aec_guard: ring clear on rising edge, AEC reset on sequence gap, "
         "status file ok");

out:
    if (sender >= 0)
        close(sender);
    le_voice_reference_close(&reference);
    le_voice_aec_destroy(&aec);
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", status) > 0)
        unlink(tmp);
    unlink(status);
    rmdir(root);
    return result;
}
