/****************************************************************************
 *  Copyright (C) 2018 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#ifndef __SCGI_H
#define __SCGI_H

#include <regex.h>
#include <glib.h>
#include <gio/gio.h>

#define SCGI_STATE_NEW          0   /* New connection - waiting for netstring length */
#define SCGI_STATE_REQ_HEADERS  1   /* Receiving the request headers. */
#define SCGI_STATE_REQ_BODY     2   /* Receiving the request body, if present. */
#define SCGI_STATE_RESPONSE     3   /* Sending the inline SCGI response data and closing when empty. */
#define SCGI_STATE_EXTRA        4   /* Sending extra body data after the inline response data. */
#define SCGI_STATE_SUBSCRIBE    5   /* Subscribed an SSE event stream */

struct scgi_header {
    const char *name;
    const char *value;
};

struct scgi_conn {
    struct scgi_conn *next;
    struct scgi_conn *prev;
    struct scgi_ctx  *ctx;
    GSocketConnection *sock;
    GInputStream *istream;
    GOutputStream *ostream;

    /* Connection state. */
    int state;
    int contentlen;
    int netstrlen;
    int numhdr;
    struct scgi_header hdr[64];
    const char *method;
    const char *path;

    /* SCGI request contents. */
    struct {
        int length;     /* Number of received bytes in the buffer. */
        int offset;     /* Offset of parsed data. */
        char buffer[1024];
        char *body;     /* Memory for the request body. */
        int bodylen;    /* Length of data received so far in the request body */
    } rx;

    /* SCGI reponse data. */
    struct {
        int length;
        int offset;
        char buffer[1024];  /* Inline data to be sent directly */
        char *extra;        /* Extra data allocated for the response body */
        size_t extralen;    /* Length of extra data for the response body */
    } tx;
};

typedef void (*scgi_request_t)(struct scgi_conn *conn, const char *method, void *closure);

struct scgi_path {
    regex_t         re;
    scgi_request_t  hook;
    void            *closure;
};

struct scgi_ctx {
    struct scgi_conn *head;
    struct scgi_conn *tail;

    /* Path handlers */
    int npaths;
    struct scgi_path *paths;

    scgi_request_t  hook;
    void            *closure;
    GSocketService  *service;
};

struct scgi_ctx *scgi_server_ctx(GSocketService *service, scgi_request_t hook, void *closure);
void scgi_ctx_register(struct scgi_ctx *ctx, const char *regex, scgi_request_t hook, void *closure);
void scgi_ctx_foreach(struct scgi_ctx *ctx, scgi_request_t hook, void *closure);

void scgi_client_error(struct scgi_conn *conn, int code, const char *message);
void scgi_start_response(struct scgi_conn *conn, int code, const char *status);

void scgi_write_header(struct scgi_conn *conn, const char *fmt, ...);
void scgi_write_payload(struct scgi_conn *conn, const char *fmt, ...);
void scgi_take_payload(struct scgi_conn *conn, void *data, size_t len);

const char *scgi_header_find(struct scgi_conn *conn, const char *name);

char *scgi_urldecode(char *input);

/* D-Bus Methods and Signal Handlers */
void scgi_introspect(struct scgi_ctx *ctx, DBusGConnection* bus, DBusGProxy *proxy);
void scgi_signal_handler(DBusGProxy* proxy, GHashTable *args, const char *name);
void scgi_error_handler(struct scgi_conn *conn, GError *error);
GValue *scgi_parse_params(struct scgi_conn *conn, const char *method);
void scgi_call_void(struct scgi_conn *conn, const char *method, void *user_data);
void scgi_call_args(struct scgi_conn *conn, const char *method, void *user_data);
void scgi_allow_cors(struct scgi_conn *conn, const char *allowed);

#endif /* __SCGI_H */
