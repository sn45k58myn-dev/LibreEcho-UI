#ifndef LE_INTERCOM_PROTOCOL_H
#define LE_INTERCOM_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define LE_IC_SAMPLES 320u
#define LE_IC_PCM_BYTES 640u
#define LE_IC_PACKET_BYTES 644u
#define LE_IC_WS_MAX 1024u
#ifndef LE_IC_SOCKET
#define LE_IC_SOCKET "/run/libreecho/intercom.sock"
#endif
#ifndef LE_IC_LEASE
#define LE_IC_LEASE "/run/libreecho/intercom.lease"
#endif
#ifndef LE_IC_ACK
#define LE_IC_ACK "/run/libreecho/intercom-assist.lease"
#endif
/* All clocks are CLOCK_MONOTONIC, not wall time. */
uint64_t le_ic_now(void);
int le_ic_lease_active(const char *path, uint64_t now);
int le_ic_lease_write(const char *path, uint64_t now);
/* Resolve the config symlink before selecting its device-local sidecar. */
int le_ic_optin_path(const char *config, char *out, size_t size);
/* Incremental masked, final RFC6455 frame. 0 incomplete, -1 invalid, 1 frame.
 * No fragments/extensions/64-bit lengths. Audio is one 20ms PCM packet. */
int le_ic_ws_decode(const unsigned char *in, size_t count, unsigned char *out,
                    size_t *length, unsigned *opcode, size_t *consumed);
size_t le_ic_ws_encode(unsigned char *out, size_t cap, unsigned opcode,
                       const void *payload, size_t length);
int le_ic_ws_accept(const char *key, char *out, size_t size);
#endif
