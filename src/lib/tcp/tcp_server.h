#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#define POSIX_C_SOURCE 200809L
#include "smw.h"

#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CLIENTS 512

typedef int (*TcpServerOnAccept)(int client_fd, void* context);

typedef struct {
    int listen_fd;

    TcpServerOnAccept onAccept;
    void*             context;

    SmwTask* task;

} TCPServer;

int tcp_server_initiate(TCPServer* server, const char* port,
                        TcpServerOnAccept on_accept, void* context);
int tcp_server_initiate_ptr(const char* port, TcpServerOnAccept on_accept,
                            void* context, TCPServer** server_ptr);

void tcp_server_dispose(TCPServer* server);
void tcp_server_dispose_ptr(TCPServer** server_ptr);

static inline int tcp_server_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#endif // TCP_SERVER_H
