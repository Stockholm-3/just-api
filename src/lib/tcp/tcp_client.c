#include "tcp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int tcp_client_initiate(TCPClient* c, int fd) {
    c->fd = fd;
    return 0;
}

int tcp_client_connect(TCPClient* c, const char* host, const char* port) {
    printf("TCP_DEBUG: tcp_client_connect called with host='%s', port='%s'\n",
           host, port);

    if (c->fd >= 0) {
        printf("TCP_DEBUG: Socket already connected (fd=%d)\n", c->fd);
        return -1;
    }

    struct addrinfo  hints = {0};
    struct addrinfo* res   = NULL;

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    printf("TCP_DEBUG: Calling getaddrinfo...\n");
    int gai_result = getaddrinfo(host, port, &hints, &res);
    if (gai_result != 0) {
        printf("TCP_DEBUG: getaddrinfo failed: %s\n", gai_strerror(gai_result));
        return -1;
    }
    printf("TCP_DEBUG: getaddrinfo succeeded\n");

    int fd = -1;
    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        printf("TCP_DEBUG: Creating socket with family=%d\n", rp->ai_family);
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        printf("TCP_DEBUG: socket() returned fd=%d\n", fd);

        if (fd < 0) {
            printf("TCP_DEBUG: socket() failed: %s\n", strerror(errno));
            continue;
        }

        printf("TCP_DEBUG: Setting non-blocking mode\n");
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        printf("TCP_DEBUG: Calling connect()...\n");
        int connect_result = connect(fd, rp->ai_addr, rp->ai_addrlen);
        printf("TCP_DEBUG: connect() returned %d, errno=%d (%s)\n",
               connect_result, errno, strerror(errno));

        if (connect_result == 0 || errno == EINPROGRESS) {
            printf("TCP_DEBUG: Connection initiated successfully\n");
            break;
        }

        printf("TCP_DEBUG: Connection failed, trying next address\n");
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd < 0) {
        printf("TCP_DEBUG: All connection attempts failed\n");
        return -1;
    }

    c->fd = fd;
    printf("TCP_DEBUG: Success! Stored fd=%d in TCPClient\n", fd);
    return 0;
}

int tcp_client_write(TCPClient* c, const uint8_t* buf, size_t len) {
    return send(c->fd, buf, len, MSG_NOSIGNAL);
}

int tcp_client_read(TCPClient* c, uint8_t* buf, size_t len) {
    /* Clear errno so we can distinguish EOF (recv==0) from previous errors */
    errno = 0;
    int n = recv(c->fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // no data available right now
        }
        return -1; // real error
    }

    if (n == 0) {
        return -2; // EOF (peer closed connection)
    }

    return n;
}

void tcp_client_disconnect(TCPClient* c) {
    if (c->fd >= 0) {
        close(c->fd);
    }

    c->fd = -1;
}

void tcp_client_dispose(TCPClient* c) { tcp_client_disconnect(c); }
