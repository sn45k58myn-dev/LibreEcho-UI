#ifndef LE_INTERCOM_H
#define LE_INTERCOM_H
#include "api.h"
#include <poll.h>
int le_ic_api(struct api_context *, const struct api_request *, struct api_response *);
/* Transfers fd only on success. No bytes after the HTTP headers are accepted. */
int le_ic_upgrade(struct api_context *, const struct api_request *, int fd,
                   const char *key, const char *version, const char *upgrade,
                   const char *connection);
void le_ic_pollfds(struct pollfd out[2]);
void le_ic_tick(struct api_context *, const struct pollfd in[2]);
void le_ic_close(void);
#endif
