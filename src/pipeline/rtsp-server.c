/****************************************************************************
 *  Copyright (C) 2019 Kron Technologies Inc <http://www.krontech.ca>.      *
 *                                                                          *
 *  This program is free software: you can redistribute it and/or modify    *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>

#include "rtsp-private.h"
#include "pipeline.h"

char *
rtsp_header_find(struct rtsp_conn *conn, const char *name)
{
    int i;
    for (i = 0; i < conn->numhdr; i++) {
        if (strcasecmp(conn->rxhdr[i].name, name) == 0) {
            return conn->rxhdr[i].value;
        }
    }
    return NULL;
}


char *
rtsp_param_find(const char *start, const char *name, char *value, size_t len)
{
    size_t namelen;
    for (namelen = strlen(name); *start != '\0'; start += strcspn(start, ";")) {
        /* Skip any leading semicolons. */
        while (*start == ';') start++;

        /* Check the start of the string for a matching name. */
        if (memcmp(start, name, namelen) != 0) continue; /* Definitely not a match. */
        if ((start[namelen] == ';') || (start[namelen] == '\0')) {
            /* Got a match with no value. */
            *value = '\0';
            return value;
        }

        if (start[namelen] == '=') {
            /* Got a match with a value. */
            const char *vstart = start + namelen + 1;
            size_t vlen = strcspn(vstart, ";");
            if (vlen > len) vlen = len;
            value[vlen] = '\0';
            return memcpy(value, start + namelen + 1, vlen);
        }
    }

    /* No such parameter */
    return NULL;
}

static void
rtsp_client_close(struct rtsp_ctx *ctx, struct rtsp_conn *conn)
{
    /* Unlink the client connection structure. */
    if (conn->next) conn->next->prev = conn->prev;
    else ctx->conn_tail = conn->prev;
    if (conn->prev) conn->prev->next = conn->next;
    else ctx->conn_head = conn->next;

    /* Garbage collect the client resources. */
    close(conn->sock);
    free(conn);
}

/* Transmit data out the RTSP client connection. */
static void
rtsp_client_send(struct rtsp_ctx *ctx, struct rtsp_conn *conn)
{
    /* Write the response data. */
    int ret = send(conn->sock, conn->tx.buffer + conn->tx.offset, conn->tx.length - conn->tx.offset, 0);
    if (ret > 0) {
        conn->tx.offset += ret;
        if (conn->tx.offset >= conn->tx.length) {
            if (conn->state == RTSP_CLIENT_FINAL) {
                rtsp_client_close(ctx, conn);
                return;
            }
            else {
                conn->state = RTSP_CLIENT_START;
                memset(&conn->tx, 0, sizeof(conn->tx));
            }
        }
    }
    else if (ret == 0) {
        rtsp_client_close(ctx, conn); /* Client closed the connection. */
    }
    else if (errno != EAGAIN) {
        rtsp_client_close(ctx, conn); /* Socket error of some kind. */
    }
}

/* Read a line from the RTSP connection buffer. */
static char *
rtsp_getline(struct rtsp_conn *conn)
{
    /* Search for the line ending */
    char *p;
    char *start = conn->rxbuffer + conn->rxoffset;
    char *end = conn->rxbuffer + conn->rxlength;
    size_t length;
    for (p = start; p < (end - 1); p++) {
        if (p[0] != '\r') continue;
        if (p[1] != '\n') continue;

        /* Terminate the string and advance the rxoffset. */
        conn->rxoffset = (p - conn->rxbuffer + 2);
        *p++ = '\0';
        return start;
    }
    return NULL;
}

void
rtsp_write_header(struct rtsp_conn *conn, const char *fmt, ...)
{
    int ret;
    
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(conn->tx.buffer + conn->tx.length, sizeof(conn->tx.buffer) - conn->tx.length, fmt, ap);
    va_end(ap);
    if (ret < 0) return;

    /* If this is the last header, keep track of its location. */
    if (ret == 0) {
        conn->tx.endhdr = conn->tx.length;
    }

    /* Terminate the line. */
    if ((conn->tx.length + ret) < sizeof(conn->tx.buffer)-2) {
        conn->tx.length += ret;
        conn->tx.buffer[conn->tx.length++] = '\r';
        conn->tx.buffer[conn->tx.length++] = '\n';
    }
}

void
rtsp_start_response(struct rtsp_conn *conn, int code, const char *status)
{
    char date[64];
    time_t now = time(NULL);
    ctime_r(&now, date);
    date[strlen(date)-1] ='\0'; /* Because ctime insists on adding a newline. */

    /* Reset the transmit buffer */
    memset(&conn->tx, 0, sizeof(conn->tx));

    /* Write the common headers. */
    rtsp_write_header(conn, "RTSP/1.0 %d %s", code, status);
    rtsp_write_header(conn, "CSeq: %d", conn->cseq);
    rtsp_write_header(conn, "Date: %s", date);
}

void
rtsp_write_payload(struct rtsp_conn *conn, const char *fmt, ...)
{
    int ret;
    
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(conn->tx.buffer + conn->tx.length, sizeof(conn->tx.buffer) - conn->tx.length, fmt, ap);
    va_end(ap);
    if (ret < 0) return;

    /* Terminate the line. */
    if ((conn->tx.length + ret) < sizeof(conn->tx.buffer)-2) {
        conn->tx.length += ret;
        conn->tx.buffer[conn->tx.length++] = '\r';
        conn->tx.buffer[conn->tx.length++] = '\n';
    }
}

void
rtsp_finish_payload(struct rtsp_conn *conn)
{
    char tmp[64];
    int clength = conn->tx.length - conn->tx.endhdr - 2;
    int hlength = snprintf(tmp, sizeof(tmp), "Content-Length: %d\r\n", clength);

    /* Ensure the addition of the content-length header will not overflow. */
    if (conn->tx.length + hlength > sizeof(conn->tx.buffer)) {
        rtsp_client_error(conn, 500, "Internal Server Error");
        return;
    }

    /* Insert the content-length header. */
    memmove(conn->tx.buffer + conn->tx.endhdr + hlength,
            conn->tx.buffer + conn->tx.endhdr,
            conn->tx.length - conn->tx.endhdr);
    memcpy(conn->tx.buffer + conn->tx.endhdr, tmp, hlength);
    conn->tx.length += hlength;
}

/* Generate an RTSP error message and disconnect from the client. */
void
rtsp_client_error(struct rtsp_conn *conn, int code, const char *message)
{
    memset(&conn->tx, 0, sizeof(conn->tx));

    rtsp_write_header(conn, "RTSP/1.0 %d %s", code, message);
    rtsp_write_header(conn, "");

    /* Send the client data. */
    conn->state = RTSP_CLIENT_FINAL;
}

static void
rtsp_client_reply(struct rtsp_ctx *ctx, struct rtsp_conn *conn)
{
    void *payload = (conn->contentlen) ? conn->rxbuffer + conn->rxoffset : NULL;

    char date[64];
    time_t now = time(NULL);
    ctime_r(&now, date);
    date[strlen(date)-1] ='\0'; /* Because ctime insists on adding a newline. */

    /* TODO: Process the request body. */
    memmove(conn->rxbuffer,
            conn->rxbuffer + conn->rxoffset + conn->contentlen,
            conn->rxlength - conn->rxoffset - conn->contentlen);

    /* Handle RTSP messages */
    if (strcasecmp(conn->method, "OPTIONS") == 0) {
        rtsp_method_options(ctx, conn, payload, conn->contentlen);
    }
    else if (strcasecmp(conn->method, "DESCRIBE") == 0) {
        rtsp_method_describe(ctx, conn, payload, conn->contentlen);
    }
    else if (strcasecmp(conn->method, "SETUP") == 0) {
        rtsp_method_setup(ctx, conn, payload, conn->contentlen);
    }
    else if (strcasecmp(conn->method, "PLAY") == 0) {
        rtsp_method_play(ctx, conn, payload, conn->contentlen);
    }
    /* Otherwise this method is not supported. */
    else {
        rtsp_write_header(conn, "RTSP/1.0 405 Method Not Allowed");
        rtsp_write_header(conn, "CSeq: %d", conn->cseq);
        rtsp_write_header(conn, "Date: %s", date);
        rtsp_write_header(conn, "");
    }

    /* Send the client data. */
    conn->state = RTSP_CLIENT_REPLY;
}

static void
rtsp_client_process(struct rtsp_ctx *ctx, struct rtsp_conn *conn)
{
    for (;;) {
        if (conn->state == RTSP_CLIENT_START) {
            /* Search for a line-ending. */
            char *line = rtsp_getline(conn);
            if (!line) return; /* Need more data. */
            fprintf(stderr, "DEBUG: rtsp_client_process line=%s\n", line);
            
            /* First token should be the request method. */
            conn->method = line;
            
            /* Second token should be the URL. */
            if (!(conn->url = strchr(line, ' '))) {
                rtsp_client_error(conn, 400, "Bad Request");
                return;
            }
            *conn->url++ = '\0';

            /* Third token should be the protocol version. */
            if (!(conn->vers = strchr(conn->url, ' '))) {
                rtsp_client_error(conn, 400, "Bad Request");
                return;
            }
            *conn->vers++ = '\0';

            /* Continue to process client header data. */
            conn->state = RTSP_CLIENT_HEADER;
            conn->contentlen = 0;
            conn->numhdr = 0;
            continue;
        }

        if (conn->state == RTSP_CLIENT_HEADER) {
            /* Search for a line-ending. */
            char *name = rtsp_getline(conn);
            char *value;
            fprintf(stderr, "DEBUG: rtsp_client_process header=%s\n", name);

            if (!name) return; /* Need more data. */
            if (strlen(name) == 0) {
                /* An empty line terminates the headers. */
                if (conn->contentlen) {
                    conn->state = RTSP_CLIENT_BODY;
                    continue;
                }
                else {
                    rtsp_client_reply(ctx, conn);
                    return;
                }
            }
            
            /* Split the name/value on the first colon. */
            value = strchr(name, ':');
            if (value) *value++ = '\0';

            /* Check for special headers. */
            if (strcasecmp(name, "CSeq") == 0) {
                conn->cseq = strtoul(value, NULL, 0);
            }
            else if (strcasecmp(name, "Content-Length") == 0) {
                conn->contentlen = strtoul(value, NULL, 0);
            }

            /* Save the header for later use. */
            if (conn->numhdr < (sizeof(conn->rxhdr)/sizeof(conn->rxhdr[0]))) {
                conn->rxhdr[conn->numhdr].name = name;
                conn->rxhdr[conn->numhdr].value = value;
                conn->numhdr++;
            }
        }

        if (conn->state == RTSP_CLIENT_BODY) {
            if (conn->rxlength + conn->rxoffset < conn->contentlen) return;
            rtsp_client_reply(ctx, conn);
            return;
        }
    }
}

/* Receive data from an RTSP client connection. */
static void
rtsp_client_recv(struct rtsp_ctx *ctx, struct rtsp_conn *conn)
{
    int len;

    while (conn->tx.length == 0) {
        if (conn->rxlength == sizeof(conn->rxbuffer)) {
            rtsp_client_error(conn, 413, "Request Entity Too Large");
            return;
        }

        len = recv(conn->sock, conn->rxbuffer + conn->rxlength, sizeof(conn->rxbuffer) - conn->rxlength, 0);
        fprintf(stderr, "DEBUG: rtsp_client_recv got %d bytes\n", len);
        if (len == 0) {
            /* Connection was closed. */
            rtsp_client_close(ctx, conn);
            return;
        }
        if (len < 0) {
            /* Handle socket errors. */
            if (errno == EAGAIN) return;
            rtsp_client_close(ctx,  conn);
            return;
        }

        /* Process any received data. */
        conn->rxlength += len;
        rtsp_client_process(ctx, conn);
    }
}

/* Handle new connections. */
static void
rtsp_server_accept(struct rtsp_ctx *ctx)
{
    struct rtsp_conn *conn;

    struct sockaddr_storage sas;
    socklen_t addrlen = sizeof(sas);
    char tmp[INET6_ADDRSTRLEN];

    int sock = accept(ctx->server_sock, (struct sockaddr *)&sas, &addrlen);
    if (sock < 0) {
        return;
    }
    
    conn = calloc(1, sizeof(struct rtsp_conn));
    if (!conn) {
        close(sock);
        return;
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);
    conn->sock = sock;
    conn->addrlen = addrlen;
    memcpy(&conn->addr, &sas, sizeof(struct sockaddr_storage));

    /* Link the new client into the end of the list. */
    conn->next = NULL;
    conn->prev = ctx->conn_tail;
    if (conn->prev) {
        conn->prev->next = conn;
        ctx->conn_tail = conn;
    }
    else {
        ctx->conn_tail = conn;
        ctx->conn_head = conn;
    }
}

static void *
rtsp_server_thread(void *arg)
{
    struct rtsp_ctx *ctx = arg;
    struct rtsp_conn *conn, *next;

    /* Prepare to accept new TCP connections. */
    if (listen(ctx->server_sock, RTSP_SOCKET_BACKLOG) < 0) {
        fprintf(stderr, "Failed to listen on RTSP server socket.\n");
        close(ctx->server_sock);
        return NULL;
    }
    fcntl(ctx->server_sock, F_SETFL, O_NONBLOCK);
    
    /* RTSP server loop. */
    for (;;) {
        fd_set rfd, wfd;
        int ret, maxfd;
        FD_ZERO(&rfd);
        FD_ZERO(&wfd);

        /* Build the list sockets to wait on. */
        maxfd = ctx->server_sock;
        FD_SET(ctx->server_sock, &rfd);
        for (conn = ctx->conn_head; conn; conn = conn->next) {
            if (maxfd < conn->sock) maxfd = conn->sock;
            if (conn->tx.length) {
                FD_SET(conn->sock, &wfd);
            }
            else {
                FD_SET(conn->sock, &rfd);
            }
        }

        /* Wait for socket activity. */
        ret = select(maxfd+1, &rfd, &wfd, NULL, NULL);
        if (ret == 0) continue;
        if (ret < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "Select failed: %s\n", strerror(errno));
            break;
        }

        /* Handle new client connections. */
        if (FD_ISSET(ctx->server_sock, &rfd)) {
            rtsp_server_accept(ctx);
        }

        /* Handle socket data from clients. */
        conn = ctx->conn_head;
        while (conn) {
            next = conn->next;

            /* If there is data to transmit, we need to wait for write. */
            if (conn->tx.length) {
                if (FD_ISSET(conn->sock, &wfd)) {
                    rtsp_client_send(ctx, conn);
                }
            }
            /* Otherwise, process incoming data, if any. */
            else if (FD_ISSET(conn->sock, &rfd)) {
                rtsp_client_recv(ctx, conn);
            }

            conn = next;
        }
    }

    /* Cleanup and exit. */
    close(ctx->server_sock);
    return NULL;
}

struct rtsp_ctx *
rtsp_server_launch(struct pipeline_state *state)
{
    struct rtsp_ctx     *ctx;
    struct sockaddr_in  sin;
    int enable = 1;

    ctx = calloc(1, sizeof(struct rtsp_ctx));
    if (!ctx) {
        fprintf(stderr, "Failed to allocate RTSP server context.\n");
        return NULL;
    }
    ctx->server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->server_sock < 0) {
        fprintf(stderr, "Failed to create RTSP server socket.\n");
        free(ctx);
        return NULL;
    }
    setsockopt(ctx->server_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(RTSP_SERVER_PORT);

    if (bind(ctx->server_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        fprintf(stderr, "Failed to bind RTSP server socket.\n");
        close(ctx->server_sock);
        free(ctx);
        return NULL;
    }

    pthread_create(&ctx->thread, NULL, rtsp_server_thread, ctx);
    return ctx;
}

void
rtsp_server_cleanup(struct rtsp_ctx *ctx)
{
    /* TODO: Signal the thread to exit correctly. */
    close(ctx->server_sock);
    //pthread_timedjoin_np(ctx->thread, NULL, &ts);
}

/* Iterate on the session list */
void
rtsp_session_foreach(struct rtsp_ctx *ctx, void (*callback)(const char *host, int port, void *closure), void *closure)
{
    char tmp[INET6_ADDRSTRLEN];

    if (ctx->dest.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ctx->dest;
        callback(inet_ntop(AF_INET, &sin->sin_addr, tmp, sizeof(tmp)), htons(sin->sin_port), closure);
    }
    else if (ctx->dest.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ctx->dest;
        callback(inet_ntop(AF_INET6, &sin6->sin6_addr, tmp, sizeof(tmp)), htons(sin6->sin6_port), closure);
    }
}
