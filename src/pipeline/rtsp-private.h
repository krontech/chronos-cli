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
#ifndef __RTSP_PRIVATE_H
#define __RTSP_PRIVATE_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "pipeline.h"

#define RTSP_SERVER_PORT        554
#define RTSP_SOCKET_BACKLOG     4
#define RTSP_SESSION_TIMEOUT    60

/* Client states. */
#define RTSP_CLIENT_START   0   /* Waiting for the RTSP request line. */
#define RTSP_CLIENT_HEADER  1   /* Waiting for a header line. */
#define RTSP_CLIENT_BODY    2   /* Waiting for the request body. */
#define RTSP_CLIENT_REPLY   3   /* Sending the RTSP response. */
#define RTSP_CLIENT_FINAL   4   /* Sending the RTSP response, and close the connection when complete. */

struct rtsp_header {
    char *name;
    char *value;
};

struct rtsp_conn {
    struct rtsp_conn *next;
    struct rtsp_conn *prev;

    int state;
    int sock;
    socklen_t addrlen;
    struct sockaddr_storage addr;

    /* RTSP client receive data. */
    int rxlength;   /* Number of received bytes in the buffer. */
    int rxoffset;   /* Offset of parsed data. */
    char rxbuffer[1024];

    /* Client state. */
    int contentlen;
    int cseq;
    int numhdr;
    struct rtsp_header rxhdr[16];
    char *method;
    char *url;
    char *vers;

    /* RTSP client transmit data. */
    struct {
        int length;
        int endhdr;
        int offset;
        char buffer[1024];
    } tx;
};

struct rtsp_sess {
    struct rtsp_sess *next;
    struct rtsp_sess *prev;

    struct sockaddr_storage dest;
    struct timespec expire;
    int sid;
    int state;
};

struct rtsp_ctx {
    pthread_t thread;
    int server_sock;
    rtsp_session_hook_t hook;
    void *closure;

    struct rtsp_conn *conn_head;
    struct rtsp_conn *conn_tail;

    /* Single session supported for now. */
    struct rtsp_sess session;
};

void rtsp_client_error(struct rtsp_conn *conn, int code, const char *message);
void rtsp_start_response(struct rtsp_conn *conn, int code, const char *status);

char *rtsp_header_find(struct rtsp_conn *conn, const char *name);
char *rtsp_param_find(const char *start, const char *name, char *value, size_t len);
void rtsp_write_header(struct rtsp_conn *conn, const char *fmt, ...);
void rtsp_write_payload(struct rtsp_conn *conn, const char *fmt, ...);
void rtsp_finish_payload(struct rtsp_conn *conn);

char *rtsp_parse_param(const char *start, const char **savep, char *out, size_t len);

struct rtsp_sess *rtsp_session_new(struct rtsp_ctx *ctx, struct sockaddr_storage *dest);
struct rtsp_sess *rtsp_session_match(struct rtsp_ctx *ctx, struct rtsp_conn *conn);

/* RTSP method functions. */
/* TODO: Maybe these should be dynamically registered in a list somewhere. */
void rtsp_method_options(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);
void rtsp_method_getparam(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);
void rtsp_method_describe(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);
void rtsp_method_setup(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);
void rtsp_method_play(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);
void rtsp_method_pause(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);
void rtsp_method_teardown(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len);

void rtsp_server_run_hook(struct rtsp_ctx *ctx, struct rtsp_sess *sess);

#endif /* __RTSP_PRIVATE_H */
