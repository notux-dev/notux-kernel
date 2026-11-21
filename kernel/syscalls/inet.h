#ifndef INET_H
#define INET_H 
#include "stdint.h"
#include "process.h"
struct iov { uint64_t base, len; };
struct isock;
struct isock *inet_new(int type);
void inet_close(struct isock *s);
int64_t inet_bind(process_t *p, struct isock *s, uint64_t uaddr, uint64_t alen);
int64_t inet_listen(struct isock *s, int backlog);
int64_t inet_connect(process_t *p, struct isock *s, int nonblock, uint64_t uaddr, uint64_t alen);
struct isock *inet_accept(struct isock *l);
int64_t inet_shutdown(struct isock *s, int how);
int64_t inet_send(process_t *p, struct isock *s, int nonblock, struct iov *iov, int niov, int mflags,
                  uint64_t uname, uint64_t unamelen);
int64_t inet_recv(process_t *p, struct isock *s, int nonblock, struct iov *iov, int niov, int mflags,
                  uint32_t *sip, uint16_t *sport);
int inet_put_name(process_t *p, uint64_t uaddr, uint32_t cap, uint32_t ip, uint16_t port, uint32_t *outlen);
int inet_sockname(struct isock *s, int peer, uint32_t *ip, uint16_t *port);
int inet_getsockopt(struct isock *s, int level, int opt, uint8_t out[16], uint32_t *n);
int inet_poll(struct isock *s);
int inet_nread(struct isock *s);
#endif
