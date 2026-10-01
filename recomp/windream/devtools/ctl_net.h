/*
 * Development control channel - transport: a TCP listener on 127.0.0.1 and a
 * queue of request lines.
 *
 * One thread accepts a client (one at a time), reads newline-terminated lines
 * and queues them. It does nothing else: the host's main thread takes the
 * lines with ctl_net_next and answers with ctl_net_send.
 */
#ifndef WD_CTL_NET_H
#define WD_CTL_NET_H

#include <stddef.h>
#include <stdint.h>

/* Listen on 127.0.0.1:port (0: any free port). Returns the port, or -1 with
 * the reason in error. */
int ctl_net_start(int port, char* error, size_t error_size);
/* The next request line (malloc'd, without its newline; free it), or NULL.
 * *connection is the client it came from. */
char* ctl_net_next(uint32_t* connection);
/* The connected client's number (they count from 1), 0 when there is none. */
uint32_t ctl_net_connection(void);
/* Send one line (the newline is added) to that client, if it is still the
 * connected one. */
void ctl_net_send(uint32_t connection, const char* line, size_t length);
/* Let what was sent reach the client, then close the connection: before the
 * process exits. */
void ctl_net_finish(void);

#endif /* WD_CTL_NET_H */
