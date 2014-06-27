/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libssh2.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <errno.h>

#include "mk_list.h"

struct stream {
    LIBSSH2_CHANNEL *channel;
    char buf[8192];
    struct mk_list _head;
};

int socket_create()
{
    int fd;
    int on = 1;

    /* create the socket and set the nonblocking flag status */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd <= 0) {
        perror("socket");
        return -1;
    }

    setsockopt(fd, SOL_TCP, TCP_NODELAY, &on, sizeof(on));
    return fd;
}

int socket_connect(int fd, char *host, char *port)
{
    int ret;
    struct addrinfo *hints = NULL;
    struct addrinfo *res = NULL;

    hints = malloc(sizeof(struct addrinfo));
    memset(hints, '\0', sizeof(struct addrinfo));
    hints->ai_family   = AF_UNSPEC;
    hints->ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(host, port, hints, &res);
    free(hints);

    if (ret != 0) {
        errno = 0;
        fprintf(stderr, "[network] get address failed: fd=%i host=%s port=%s\n",
                fd, host, port);
        return -1;
    }

    ret = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    return ret;
}

/* Cleanup and remove a proxy connection */
void stream_remove(struct stream *s)
{
    libssh2_channel_wait_closed(s->channel);
    libssh2_channel_free(s->channel);
    mk_list_del(&s->_head);
    free(s);
}

void session_loop(int server_fd, LIBSSH2_LISTENER *listener)
{

    int i;
    int fd;
    int efd;
    int ret;
    int num_fds;
    int bytes;
    struct epoll_event event = {0, {0}};
    struct epoll_event *events;
    struct mk_list channels;
    struct mk_list *head;
    struct mk_list *tmp;
    struct stream *st;
    LIBSSH2_CHANNEL *channel;

    mk_list_init(&channels);

    efd = epoll_create1(EPOLL_CLOEXEC);
    if (efd == -1) {
        perror("epoll");
    }

    event.data.fd = server_fd;
    event.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, server_fd, &event);
    if (ret == -1) {
        perror("epoll_ctl");
    }

    events = calloc(32 * sizeof(struct epoll_event), 1);

    fprintf(stderr, "Waiting for events...\n");

    while (1) {
        num_fds = epoll_wait(efd, events, 32, -1);

        for (i = 0; i < num_fds; i++) {
            fd = events[i].data.fd;

            if (events[i].events & EPOLLIN) {
                /* Event on libssh2 Session socket */
                if (fd == server_fd) {
                    /* Check if we have a new channel request */
                    channel = libssh2_channel_forward_accept(listener);
                    if (channel) {
                        /* After accept the connection, register the Channel data */
                        st = malloc(sizeof(struct stream));
                        st->channel = channel;
                        mk_list_add(&st->_head, &channels);

                        fprintf(stderr, "[+] new channel connection %p\n", channel);
                    }

                    /* Check if the incoming packet is for some active Channel */
                    mk_list_foreach_safe(head, tmp, &channels) {
                        st = mk_list_entry(head, struct stream, _head);
                        bytes = libssh2_channel_read(st->channel,
                                                     st->buf,
                                                     sizeof(st->buf));

                        if (bytes == LIBSSH2_ERROR_EAGAIN || bytes == 0) {
                            continue;
                        }

                        if (bytes == LIBSSH2_ERROR_CHANNEL_CLOSED) {
                            stream_remove(st);
                            continue;
                        }
                        else if (bytes < 0) {
                            printf("CHANNEL CLOSED\n");
                            stream_remove(st);
                        }
                        else {
                            printf("[+] Channel %p READ %i bytes\n",
                                   st->channel, bytes);
                            st->buf[bytes] = '\0';

                            /* Test direct response */
                            int i;
                            int c;
                            char *b = "HTTP/1.0 200 OK\r\n"
                                     "Connection: close\r\n"
                                     "Content-Length: 10\r\n\r\n"
                                     "abcd efgh\n";

                            c = 0;
                            bytes = strlen(b);
                            do {
                                i = libssh2_channel_write(st->channel,
                                                          b + c,
                                                          bytes - c);
                                if (i == LIBSSH2_ERROR_EAGAIN) {
                                    continue;
                                }

                                if (i < 0) {
                                    fprintf(stderr, "libssh2_channel_write: %d\n", i);
                                    stream_remove(st);
                                    break;
                                }
                                c += i;
                            } while (i > 0 && c < bytes);
                            printf("[+] Channel %p completed, sent %i/%i byte\n",
                                   st->channel, c, bytes);
                            stream_remove(st);
                        }
                    }
                }
            }
            else { /* EPOLLERR | EPOLLRDHUP .. */
                printf("Not handled\n");
                exit(1);
            }
        }
    }
}

int main(int argc, char **argv)
{
    int rc;
    int server_fd;
    int remote_listenport;
    int remote_wantport = 9000;
    char *user = "edsiper";
    LIBSSH2_SESSION  *session;
    LIBSSH2_LISTENER *listener = NULL;

    fprintf(stderr,
            "== This is a test case to figure out where the problem "
            "is when using LibSSH2 when forwarding a TCP port\n\n");

    rc = libssh2_init (0);
    if (rc != 0) {
        fprintf(stderr, "Crypto initialization failed\n");
        exit(EXIT_FAILURE);
    }

    /* Connect to server */
    server_fd = socket_create();
    rc = socket_connect(server_fd, "localhost", "22");
    if (rc != 0) {
        return -1;
    }

    fprintf(stderr, "Connected to localhost:22\n");

    session = libssh2_session_init();
    if (!session) {
        return -1;
    }

    rc = libssh2_session_handshake(session, server_fd);
    if(rc) {
        fprintf(stderr, "Error when starting up SSH session: %d\n", rc);
        return -1;
    }

    /* check what authentication methods are available */
    char *userauthlist;
    userauthlist = libssh2_userauth_list(session,
                                         user,
                                         strlen(user));
    if (!strstr(userauthlist, "publickey")) {
        return -1;
    }

    rc = libssh2_userauth_publickey_fromfile(session,
                                             user,
                                             "id_rsa.pub",
                                             "id_rsa.prv",
                                             "");
    if (rc != 0) {
        fprintf(stderr, "Authentication failed\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Authentication OK\n");

    fprintf(stderr, "Request forward => localhost:%i\n", remote_wantport);
    listener = libssh2_channel_forward_listen_ex(session, "localhost",
                                                 remote_wantport,
                                                 &remote_listenport, 1);
    if (!listener) {
        fprintf(stderr, "Could not create the listener\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Listener created\n");

    /* Set non-blocking mode for active session */
    libssh2_session_set_blocking(session, 0);

    /* Start the server loop */
    session_loop(server_fd, listener);

    return 0;
}
