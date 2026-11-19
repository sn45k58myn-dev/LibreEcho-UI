#include "intercom.h"
#include "intercom_protocol.h"
#include "config_store.h"
#include "json.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* One call, four outbound frames, two inbound frames. Backpressure ends the
 * call instead of turning a live conversation into a delayed recording. */
static struct {
    int ws, local, authenticated, ready;
    char ticket[65], id[65], login[65], origin[256];
    uint64_t expires, started, seen, auth_deadline;
    unsigned char rx[2 * (LE_IC_WS_MAX + 8)], tx[4 * (LE_IC_WS_MAX + 4)];
    size_t rn, tn;
    uint32_t receive_sequence, send_sequence;
} call = { .ws = -1, .local = -1 };

static int equal(const char *a, const char *b)
{
    size_t i, n = strlen(a), m = strlen(b); unsigned diff = (unsigned)(n ^ m);
    for (i = 0; i < n; i++) diff |= (unsigned char)a[i] ^ (i < m ? (unsigned char)b[i] : 0u);
    return diff == 0;
}
static void reply(struct api_response *r, int status, const char *data, const char *error)
{
    r->status = status; strcpy(r->type, "application/json; charset=utf-8");
    if (error) snprintf(r->body, sizeof r->body,
        "{\"ok\":false,\"data\":null,\"error\":{\"code\":\"intercom_unavailable\",\"message\":\"%s\"}}", error);
    else snprintf(r->body, sizeof r->body, "{\"ok\":true,\"data\":%s,\"error\":null}", data);
    r->length = strlen(r->body);
}
static int exact_origin(const struct api_request *q)
{
    char expected[264];
    if (!q->https || !q->host[0] || strpbrk(q->host, "/\\@?# \t\r\n")) return 0;
    if (snprintf(expected, sizeof expected, "https://%s", q->host) >= (int)sizeof expected) return 0;
    return equal(q->origin, expected);
}
static int login_valid(struct api_context *c, const char *token)
{
    return c->auth.enabled && token[0] && le_auth_session(&c->auth, token, NULL, 0);
}
static int strict_security(struct api_context *c, const struct api_request *q,
                           struct api_response *r, int mutation)
{
    if (!api_request_authorize(c, q, r)) return 0;
    if (!q->https || strncmp(q->authorization, "Bearer ", 7) ||
        !login_valid(c, q->authorization + 7)) {
        reply(r, 403, NULL, "Intercom requires HTTPS and a signed-in device account"); return 0;
    }
    if (mutation && (!exact_origin(q) || !equal(q->csrf, c->csrf_token))) {
        reply(r, 403, NULL, "Intercom requires the exact HTTPS origin and CSRF token"); return 0;
    }
    return 1;
}
static int optin_path(struct api_context *c, char *path, size_t n)
{
    return le_ic_optin_path(c->config_path, path, n);
}
static int enabled(struct api_context *c)
{
    char path[512], b[80]; int v = 0, n;
    if (!optin_path(c, path, sizeof path)) return 0;
    n = config_read(path, b, sizeof b);
    return n > 0 && json_valid_object(b, (size_t)n) &&
        !json_duplicate_key(b, (size_t)n, "enabled") &&
        json_get_top_level_bool(b, (size_t)n, "enabled", &v) > 0 && v;
}
void le_ic_close(void)
{
    if (call.ws >= 0) close(call.ws);
    if (call.local >= 0) close(call.local);
    memset(&call, 0, sizeof call); call.ws = call.local = -1;
}
static int random_ticket(char out[65])
{
    unsigned char b[32]; static const char hex[] = "0123456789abcdef";
    size_t n = 0, i; int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    while (n < sizeof b) { ssize_t z = read(fd, b + n, sizeof b - n);
        if (z < 0 && errno == EINTR) continue;
        if (z <= 0) { close(fd); return -1; } n += (size_t)z;
    }
    close(fd);
    for (i = 0; i < sizeof b; i++) { out[i * 2] = hex[b[i] >> 4]; out[i * 2 + 1] = hex[b[i] & 15]; }
    out[64] = 0; memset(b, 0, sizeof b); return 0;
}
int le_ic_api(struct api_context *c, const struct api_request *q, struct api_response *r)
{
    char data[384], path[512]; uint64_t now = le_ic_now();
    if (strcmp(q->path, "/api/v1/intercom") && strcmp(q->path, "/api/v1/intercom/call")) return 0;
    if (!strict_security(c, q, r, strcmp(q->method, "GET") != 0)) return 1;
    if (call.ws < 0 && call.expires && now >= call.expires) le_ic_close();
    if (!strcmp(q->path, "/api/v1/intercom") && !strcmp(q->method, "GET")) {
        snprintf(data, sizeof data, "{\"enabled\":%s,\"busy\":%s,\"transport\":\"pcm16-wss-v1\",\"foreground_only\":true}",
                 enabled(c) ? "true" : "false", call.expires || call.ws >= 0 ? "true" : "false");
        reply(r, 200, data, NULL); return 1;
    }
    if (!strcmp(q->path, "/api/v1/intercom") && !strcmp(q->method, "PUT")) {
        struct json_member m[1]; size_t count; int v;
        if (!q->body || json_object_members(q->body, q->body_len, m, 1, &count) || count != 1 ||
            m[0].name_len != 7 || memcmp(m[0].name, "enabled", 7) ||
            json_bool_span(m[0].value, m[0].value_len, &v) != 1) {
            reply(r, 400, NULL, "Supply exactly one boolean enabled field"); return 1;
        }
        if (!optin_path(c, path, sizeof path) || config_write_atomic(path,
                v ? "{\"enabled\":true}\n" : "{\"enabled\":false}\n", v ? 17 : 18)) {
            reply(r, 503, NULL, "Speaker permission could not be saved"); return 1;
        }
        if (!v) le_ic_close();
        reply(r, 200, v ? "{\"enabled\":true}" : "{\"enabled\":false}", NULL); return 1;
    }
    if (!strcmp(q->path, "/api/v1/intercom/call") && !strcmp(q->method, "DELETE")) {
        char id[65]; struct json_member m[1]; size_t count;
        if (!q->body || json_object_members(q->body, q->body_len, m, 1, &count) || count != 1 ||
            m[0].name_len != 2 || memcmp(m[0].name, "id", 2) ||
            json_string_span(m[0].value, m[0].value_len, id, sizeof id) != 1 || strlen(id) != 64)
            reply(r, 400, NULL, "Supply the call id to end this call");
        else if (call.id[0] && (!equal(call.login, q->authorization + 7) || !equal(call.id, id)))
            reply(r, 409, NULL, "That call is no longer current");
        else { le_ic_close(); reply(r, 200, "{\"ended\":true}", NULL); } return 1;
    }
    if (!strcmp(q->path, "/api/v1/intercom/call") && !strcmp(q->method, "POST")) {
        if (!enabled(c) || c->do_not_disturb || strcmp(c->voice_pipeline_mode, "home-assistant")) {
            reply(r, 409, NULL, "Enable speaker calling, turn off Do Not Disturb, and select Home Assistant voice"); return 1;
        }
        if (call.expires || call.ws >= 0) { reply(r, 409, NULL, "The speaker already has a call"); return 1; }
        if (random_ticket(call.ticket) || random_ticket(call.id)) { le_ic_close(); reply(r, 503, NULL, "A secure call ticket could not be created"); return 1; }
        snprintf(call.login, sizeof call.login, "%s", q->authorization + 7);
        snprintf(call.origin, sizeof call.origin, "%s", q->origin);
        call.expires = now + 30000u;
        snprintf(data, sizeof data, "{\"ticket\":\"%s\",\"id\":\"%s\",\"expires_in_ms\":30000,\"path\":\"/api/v1/intercom/stream\"}", call.ticket, call.id);
        reply(r, 201, data, NULL); return 1;
    }
    reply(r, 405, NULL, "Method not allowed"); return 1;
}
static int connection_upgrade(const char *value)
{
    const char *p = value;
    while (*p) {
        const char *end = strchr(p, ','); size_t n;
        if (!end) end = p + strlen(p);
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        n = (size_t)(end - p);
        while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        if (n == 7 && !strncasecmp(p, "Upgrade", 7)) return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}
int le_ic_upgrade(struct api_context *c, const struct api_request *q, int fd,
                   const char *key, const char *version, const char *upgrade,
                   const char *connection)
{
    char accept[64], response[256]; int n, flags;
    if (strcmp(q->method, "GET") || !exact_origin(q) || !equal(call.origin, q->origin) ||
        !call.expires || le_ic_now() >= call.expires || call.ws >= 0 ||
        !login_valid(c, call.login) || !enabled(c) || c->do_not_disturb ||
        strcmp(version, "13") || strcasecmp(upgrade, "websocket") ||
        !connection_upgrade(connection) || le_ic_ws_accept(key, accept, sizeof accept)) return -1;
    n = snprintf(response, sizeof response,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    /* The small handshake must fit in the local relay socket in one write. */
    if (send(fd, response, (size_t)n, MSG_DONTWAIT | MSG_NOSIGNAL) != n) return -1;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) return -1;
    call.ws = fd; call.auth_deadline = le_ic_now() + 5000u; return 0;
}
static int queue(unsigned op, const void *data, size_t n)
{
    size_t z = le_ic_ws_encode(call.tx + call.tn, sizeof call.tx - call.tn, op, data, n);
    if (!z) return -1;
    call.tn += z; return 0;
}
static int local_start(void)
{
    struct sockaddr_un a; int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", LE_IC_SOCKET);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) || send(fd, "LEIC1", 5, MSG_NOSIGNAL) != 5) { close(fd); return -1; }
    call.local = fd; return 0;
}
void le_ic_pollfds(struct pollfd p[2])
{
    p[0] = (struct pollfd){call.ws, (short)(POLLIN | (call.tn ? POLLOUT : 0)), 0};
    p[1] = (struct pollfd){call.local, POLLIN, 0};
}
void le_ic_tick(struct api_context *c, const struct pollfd p[2])
{
    uint64_t now = le_ic_now(); unsigned char frame[LE_IC_WS_MAX + 1];
    size_t z, used; unsigned op; int rc; ssize_t n; unsigned iterations;
    if (call.ws < 0) { if (call.expires && now >= call.expires) le_ic_close(); return; }
    if (!enabled(c) || c->do_not_disturb || strcmp(c->voice_pipeline_mode, "home-assistant") ||
        !login_valid(c, call.login) || (call.authenticated && now - call.started > 600000u) ||
        (!call.authenticated && now > call.auth_deadline) ||
        (call.authenticated && now - call.seen > 6000u) ||
        (p[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
        (p[1].revents & (POLLERR | POLLHUP | POLLNVAL))) goto fail;
    if (p[0].revents & POLLIN) {
        n = recv(call.ws, call.rx + call.rn, sizeof call.rx - call.rn, MSG_DONTWAIT);
        if (n <= 0) { if (n < 0 && (errno == EAGAIN || errno == EINTR)) return; goto fail; }
        call.rn += (size_t)n;
    }
    for (iterations = 0; iterations < 8; iterations++) {
        rc = le_ic_ws_decode(call.rx, call.rn, frame, &z, &op, &used);
        if (rc < 0) goto fail;
        if (!rc) break;
        memmove(call.rx, call.rx + used, call.rn - used); call.rn -= used;
        if (op == 8) goto fail;
        if (!call.authenticated) {
            char ticket[65]; struct json_member m[1]; size_t count;
            if (op != 1 || json_object_members((char *)frame, z, m, 1, &count) || count != 1 ||
                m[0].name_len != 6 || memcmp(m[0].name, "ticket", 6) ||
                json_string_span(m[0].value, m[0].value_len, ticket, sizeof ticket) != 1 ||
                !equal(ticket, call.ticket) || now >= call.expires || !login_valid(c, call.login)) goto fail;
            memset(call.ticket, 0, sizeof call.ticket); call.expires = 0;
            call.authenticated = 1; call.started = call.seen = now;
            if (local_start()) goto fail;
            continue;
        }
        if (op == 9) { if (queue(10, frame, z)) goto fail; call.seen = now; continue; }
        if (op == 10) { call.seen = now; continue; }
        if (op == 1 && z == 4 && !memcmp(frame, "ping", 4)) {
            call.seen = now; if (queue(1, "pong", 4)) goto fail; continue;
        }
        if (op != 2 || !call.ready || z != LE_IC_PACKET_BYTES) goto fail;
        { uint32_t seq = (uint32_t)frame[0] << 24 | (uint32_t)frame[1] << 16 |
                         (uint32_t)frame[2] << 8 | frame[3];
          if (seq != call.receive_sequence++) goto fail; }
        if (send(call.local, frame + 4, LE_IC_PCM_BYTES, MSG_DONTWAIT | MSG_NOSIGNAL) != LE_IC_PCM_BYTES) goto fail;
        call.seen = now;
    }
    if (call.rn == sizeof call.rx) goto fail;
    if (p[1].revents & POLLIN) {
        for (iterations = 0; iterations < 4; iterations++) {
            n = recv(call.local, frame + 4, LE_IC_PCM_BYTES + 1, MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
            if (!call.ready) {
                if (n != 5 || memcmp(frame + 4, "READY", 5)) goto fail;
                call.ready = 1;
                if (queue(1, "ready", 5)) goto fail;
                continue;
            }
            if (n != LE_IC_PCM_BYTES) goto fail;
            frame[0] = (unsigned char)(call.send_sequence >> 24); frame[1] = (unsigned char)(call.send_sequence >> 16);
            frame[2] = (unsigned char)(call.send_sequence >> 8); frame[3] = (unsigned char)call.send_sequence++;
            if (queue(2, frame, LE_IC_PACKET_BYTES)) goto fail;
        }
    }
    if (call.tn) {
        n = send(call.ws, call.tx, call.tn, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n > 0) { memmove(call.tx, call.tx + n, call.tn - (size_t)n); call.tn -= (size_t)n; }
        else if (!n || (errno != EAGAIN && errno != EINTR)) goto fail;
    }
    return;
fail: le_ic_close();
}
