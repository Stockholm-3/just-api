#include "tcp_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

//-----------------Internal Functions-----------------

void tcp_server_task_work(void* context, uint64_t mon_time);

//----------------------------------------------------

int tcp_server_initiate(TCPServer* server, const char* port,
                        TcpServerOnAccept on_accept, void* context) {
    server->onAccept = on_accept;
    server->context  = context;

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0) {
        return -1;
    }

    if (listen(fd, MAX_CLIENTS) < 0) {
        close(fd);
        return -1;
    }

    tcp_server_nonblocking(fd);

    server->listen_fd = fd;

    server->task = smw_create_task(server, tcp_server_task_work);

    return 0;
}

int tcp_server_initiate_ptr(const char* port, TcpServerOnAccept on_accept,
                            void* context, TCPServer** server_ptr) {
    if (server_ptr == NULL) {
        return -1;
    }

    TCPServer* server = (TCPServer*)malloc(sizeof(TCPServer));
    if (server == NULL) {
        return -2;
    }

    int result = tcp_server_initiate(server, port, on_accept, context);
    if (result != 0) {
        free(server);
        return result;
    }

    *(server_ptr) = server;

    return 0;
}

int tcp_server_accept(TCPServer* server) {
    int socket_fd = accept(server->listen_fd, NULL, NULL);
    if (socket_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // ingen ny klient
        }

        perror("accept");
        return -1;
    }

    tcp_server_nonblocking(socket_fd);

    int result = server->onAccept(socket_fd, server->context);
    if (result != 0) {
        close(socket_fd);
    }

    return 0;
}

void tcp_server_task_work(void* context, uint64_t mon_time) {
    TCPServer* server = (TCPServer*)context;

    tcp_server_accept(server);
}

void tcp_server_dispose(TCPServer* server) { smw_destroy_task(server->task); }

void tcp_server_dispose_ptr(TCPServer** server_ptr) {
    if (server_ptr == NULL || *(server_ptr) == NULL) {
        return;
    }

    tcp_server_dispose(*(server_ptr));
    free(*(server_ptr));
    *(server_ptr) = NULL;
}
