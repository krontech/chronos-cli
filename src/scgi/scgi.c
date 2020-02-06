/****************************************************************************
 *  Copyright (C) 2017-2018 Kron Technologies Inc <http://www.krontech.ca>. *
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
#include <getopt.h>
#include <errno.h>
#include <glib.h>
#include <gio/gio.h>
#include <dbus/dbus-glib.h>

#include "jsmn.h"
#include "scgi.h"
#include "dbus-json.h"
#include "api/cam-rpc.h"

static void scgi_data_ready(GObject *obj, GAsyncResult *result, gpointer user_data);
static void scgi_send_done(GObject *obj, GAsyncResult *result, gpointer user_data);
static void scgi_conn_destroy(struct scgi_conn *conn);

/* Helper to request more data from an SCGI connection before continuing. */
static int
scgi_want_more(struct scgi_conn *conn)
{
    char *buffer;
    int length;

    /* Receive data into the inline buffer, or request body */
    if (conn->state == SCGI_STATE_REQ_BODY) {
        buffer = conn->rx.body + conn->rx.bodylen;
        length = conn->contentlen - conn->rx.bodylen;
    }
    else {
        buffer = conn->rx.buffer + conn->rx.length;
        length = sizeof(conn->rx.buffer) - conn->rx.length;
    }
    /* Request too large */
    if (length <= 0) {
        scgi_client_error(conn, 413, "Request Entity Too Large");
        return -1;
    }
    
    /* Start another async read */
    g_input_stream_read_async(conn->istream, buffer, length,
                            G_PRIORITY_DEFAULT, NULL,
                            scgi_data_ready, conn);
    return 0;
}

/* Start an async write and return the number of bytes left to send */
static size_t
scgi_send_more(struct scgi_conn *conn)
{
    char *buffer;
    int length;

    /* Receive data into the inline buffer, or request body */
    switch (conn->state) {
        case SCGI_STATE_RESPONSE:
            buffer = conn->tx.buffer + conn->tx.offset;
            length = conn->tx.length - conn->tx.offset;
            if (length > 0) break;
            /* Fall-through to the EXTRA state. */
            conn->state = SCGI_STATE_EXTRA;
            conn->tx.offset = 0;
        case SCGI_STATE_EXTRA:
            buffer = conn->tx.extra + conn->tx.offset;
            length = conn->tx.extralen - conn->tx.offset;
            break;

        case SCGI_STATE_SUBSCRIBE:
            buffer = conn->tx.buffer + conn->tx.offset;
            length = conn->tx.length - conn->tx.offset;
            break;

        default:
            /* don't send anything */
            return 0;
    }

    /* Get out now if there is nothing to send */
    if (length <= 0) {
        return 0;
    }
    if (!g_output_stream_has_pending(conn->ostream)) {
        /* Start another async write */
        g_output_stream_write_async(conn->ostream, buffer, length,
                                G_PRIORITY_DEFAULT, NULL,
                                scgi_send_done, conn);
    }
    return length;
}

static void
scgi_send_done(GObject *obj, GAsyncResult *result, gpointer user_data)
{
    struct scgi_conn *conn = user_data;
    GError *error = NULL;
    int count = g_output_stream_write_finish(G_OUTPUT_STREAM(obj), result, &error);
    
    /* Check for errors */
    if (count < 0) {
        scgi_conn_destroy(conn);
        return;
    }

    conn->tx.offset += count;
    switch (conn->state) {
        case SCGI_STATE_RESPONSE:
        case SCGI_STATE_EXTRA:
            if (scgi_send_more(conn) == 0) {
                /* Response has been sent - cleanup */
                g_io_stream_close(G_IO_STREAM(conn->sock), NULL, &error);
                scgi_conn_destroy(conn);
            }
            break;
        
        case SCGI_STATE_SUBSCRIBE:
            /* Make room for more data */
            conn->tx.length -= conn->tx.offset;
            memmove(conn->tx.buffer, conn->tx.buffer + conn->tx.offset, conn->tx.length);
            conn->tx.offset = 0;

            /* Try to send more data, if any */
            scgi_send_more(conn);
            break;

        default:
            break;
    }
}

static int
scgi_process_data(struct scgi_conn *conn)
{
    if (conn->state == SCGI_STATE_NEW) {
        /* Parse the netstring length */
        char *end;
        char *sep = memchr(conn->rx.buffer, ':', conn->rx.length);
        if (!sep) {
            return scgi_want_more(conn);
        }
        *sep = '\0';
        conn->netstrlen = strtoul(conn->rx.buffer, &end, 10);
        if ((conn->netstrlen == 0) || (end != sep)) {
            /* Not a valid netstring */
            return -1;
        }

        /* Continue parsing the request headers */
        conn->rx.offset = (end - conn->rx.buffer) + 1;
        conn->state = SCGI_STATE_REQ_HEADERS;
        conn->contentlen = 0;
        conn->method = "";
        conn->path = "";
    }

    if (conn->state == SCGI_STATE_REQ_HEADERS) {
        int i;
        char *netstrend = &conn->rx.buffer[conn->rx.offset + conn->netstrlen];
        char *name;
        if (conn->rx.length <= (conn->rx.offset + conn->netstrlen)) {
            return scgi_want_more(conn);
        }
        if (*netstrend != ',') {
            return -1;
        }

        /* Parse the request headers */
        name = name = &conn->rx.buffer[conn->rx.offset];
        while (name < netstrend) {
            char *value, *end;
            if ((value = memchr(name, '\0', netstrend - name)) == NULL) break;
            value++;
            if ((end = memchr(value, '\0', netstrend - value)) == NULL) break;
            end++;

            /* Append the header */
            if (conn->numhdr < sizeof(conn->hdr)/sizeof(struct scgi_header)) {
                conn->hdr[conn->numhdr].name = name;
                conn->hdr[conn->numhdr].value = value;
                conn->numhdr++;
            }
            /* Check for required headers */
            if (strcmp(name, "CONTENT_LENGTH") == 0) {
                conn->contentlen = strtoul(value, NULL, 10);
            }
            else if (strcmp(name, "REQUEST_METHOD") == 0) {
                conn->method = value;
            }
            else if (strcmp(name, "PATH_INFO") == 0) {
                conn->path = value;
            }

            /* Process the next header */
            name = end;
        }
        conn->rx.offset = (netstrend - conn->rx.buffer) + 1;

#ifdef DEBUG
        for (i = 0; i < conn->numhdr; i++) {
            fprintf(stderr, "DEBUG: %s=%s\n", conn->hdr[i].name, conn->hdr[i].value);
        }
#endif

        /* Determine how to handle the request body */
        if (conn->contentlen < (sizeof(conn->rx.buffer) - conn->rx.offset)) {
            /* Just use the inline buffer */
            conn->rx.body = &conn->rx.buffer[conn->rx.offset];
            conn->rx.bodylen = conn->rx.length - conn->rx.offset;
        }
        else {
            /* Allocation Required. */
            conn->rx.body = (conn->contentlen < (1024 * 1024)) ? g_malloc(conn->contentlen) : NULL;
            if (!conn->rx.body) {
                scgi_client_error(conn, 413, "Request Entity Too Large");
                return 0;
            }
            conn->rx.bodylen = conn->rx.length - conn->rx.offset;
            memcpy(conn->rx.body, &conn->rx.buffer[conn->rx.offset], conn->rx.bodylen);
        }
        /* Continue parsing the request body, if present */
        conn->state = SCGI_STATE_REQ_BODY;
    }

    if (conn->state == SCGI_STATE_REQ_BODY) {
        const char *path = conn->path;
        int i;

        /* Do nothing until we receive the body. */
        if (conn->rx.bodylen < conn->contentlen) {
            return scgi_want_more(conn);
        }

        /* Handle the SCGI request */
        conn->state = SCGI_STATE_RESPONSE;

        /* Search for registered paths */
        while (*path == '/') path++;
        for (i = 0; i < conn->ctx->npaths; i++) {
            struct scgi_path *p = &conn->ctx->paths[i];
            if (!p->hook) continue;
            if (regexec(&p->re, path, 0, NULL, 0) != 0) continue;
            p->hook(conn, conn->method, p->closure);
            return 0;
        }

        /* Handle everything else if a global hook was provided */
        if (conn->ctx->hook) {
            conn->ctx->hook(conn, conn->method, conn->ctx->closure);
        }
        /* Otherwise, it's a 404 error. */
        else {
            scgi_client_error(conn, 404, "Not Found");
        }
    }

    /* Otherwise, nothing to do. */
    return 0;
}

static void
scgi_conn_destroy(struct scgi_conn *conn)
{
    /* Unlink from the server contenxt */
    if (conn->next) conn->next->prev = conn->prev;
    else conn->ctx->tail = conn->prev;
    if (conn->prev) conn->prev->next = conn->next;
    else conn->ctx->head = conn->next;

    /* If the body points outside of the inline buffer, free it. */
    if (conn->rx.body) {
        char *bufend = &conn->rx.buffer[sizeof(conn->rx.buffer)];
        if (conn->rx.body < conn->rx.buffer) g_free(conn->rx.body);
        else if (conn->rx.body > bufend) g_free(conn->rx.body);
    }
    /* Clear up any extra response body data */
    if (conn->tx.extra) free(conn->tx.extra);

    /* Free the resources */
    g_object_unref(conn->sock);
    g_free(conn);
}

static void
scgi_data_ready(GObject *obj, GAsyncResult *result, gpointer user_data)
{
    struct scgi_conn *conn = user_data;
    GError *error;
    int ret;
    int count = g_input_stream_read_finish(G_INPUT_STREAM(obj), result, &error);

    /* Check for errors */
    if (count < 0) {
        scgi_conn_destroy(conn);
        return;
    }

    /* Receive the request body data. */
    if (conn->state == SCGI_STATE_REQ_BODY) {
        conn->rx.bodylen += count;
        if (conn->rx.bodylen > conn->contentlen) {
            scgi_client_error(conn, 413, "Request Entity Too Large");
        }
    }
    /* Receive the request header data. */
    else {
        conn->rx.length += count;
        if (conn->rx.length > sizeof(conn->rx.buffer)) {
            scgi_client_error(conn, 413, "Request Entity Too Large");
        }
    }

    if (scgi_process_data(conn) < 0) {
        scgi_conn_destroy(conn);
        return;
    }

    /* If there is data to be sent - send it. */
    scgi_send_more(conn);
}

static gboolean
scgi_accept(GSocketService *service, GSocketConnection *gconn, GObject *obj, gpointer user_data)
{
    struct scgi_conn *conn = g_malloc0(sizeof(struct scgi_conn));
    struct scgi_ctx *ctx = user_data;
    if (!conn) {
        fprintf(stderr, "Connection allocation failed!\n");
        return FALSE;
    }

    /* Perform async socket data handling */
    conn->ctx = ctx;
    conn->state = SCGI_STATE_NEW;
    conn->sock = g_object_ref(gconn);
    conn->istream = g_io_stream_get_input_stream(G_IO_STREAM(gconn));
    conn->ostream = g_io_stream_get_output_stream(G_IO_STREAM(gconn));
    g_input_stream_read_async(conn->istream, 
                              conn->rx.buffer, sizeof(conn->rx.buffer),
                              G_PRIORITY_DEFAULT,
                              NULL,
                              scgi_data_ready,
                              conn);
    
    /* Link this connection into the end of the list. */
    conn->next = NULL;
    conn->prev = ctx->tail;
    if (conn->prev) {
        conn->prev->next = conn;
        ctx->tail = conn;
    }
    else {
        ctx->tail = conn;
        ctx->head = conn;
    }

    return FALSE;
}

/* Create a new SCGI server context. */
struct scgi_ctx *
scgi_server_ctx(GSocketService *service, scgi_request_t hook, void *closure)
{
    struct scgi_ctx *ctx = g_malloc0(sizeof(struct scgi_ctx));
    if (!ctx) {
        return NULL;
    }
    ctx->head = NULL;
    ctx->tail = NULL;
    ctx->hook = hook;
    ctx->closure = closure;
    ctx->service = service;
    g_signal_connect(ctx->service, "incoming", G_CALLBACK(scgi_accept), ctx);
    return ctx;
}

/* Generate an SCGI error message and disconnect from the client. */
void
scgi_client_error(struct scgi_conn *conn, int code, const char *message)
{
    memset(&conn->tx, 0, sizeof(conn->tx));

    scgi_write_header(conn, "Status: %d %s", code, message);
    scgi_write_header(conn, "");

    /* Send the client data. */
    conn->state = SCGI_STATE_RESPONSE;
}

void
scgi_write_header(struct scgi_conn *conn, const char *fmt, ...)
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
scgi_start_response(struct scgi_conn *conn, int code, const char *status)
{
    /* Reset the transmit buffer */
    memset(&conn->tx, 0, sizeof(conn->tx));

    /* Write the common headers. */
    scgi_write_header(conn, "Status: %d %s", code, status);
}

void
scgi_write_payload(struct scgi_conn *conn, const char *fmt, ...)
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
scgi_take_payload(struct scgi_conn *conn, void *data, size_t len)
{
    if (conn->tx.extra) free(conn->tx.extra);
    conn->tx.extra = data;
    conn->tx.extralen = len;
}

const char *
scgi_header_find(struct scgi_conn *conn, const char *name)
{
    int i;
    for (i = 0; i < conn->numhdr; i++) {
        if (strcasecmp(conn->hdr[i].name, name) == 0) {
            return conn->hdr[i].value;
        }
    }
    return NULL;
}

void
scgi_ctx_foreach(struct scgi_ctx *ctx, scgi_request_t hook, void *closure)
{
    struct scgi_conn *conn;
    for (conn = ctx->head; conn; conn = conn->next) {
        hook(conn, "", closure);

        /* If data is ready to be written, write it */
        if (conn->tx.offset < conn->tx.length) {
            scgi_send_more(conn);
        }
    }
}

/* Register a path handler for the SCGI context */
void
scgi_ctx_register(struct scgi_ctx *ctx, const char *pattern, scgi_request_t hook, void *closure)
{
    struct scgi_path *newmem = g_realloc_n(ctx->paths, sizeof(struct scgi_path), ctx->npaths + 1);
    if (!newmem) {
        return;
    }
    ctx->paths = newmem;
    
    /* Compile the regex match for the path */
    if (regcomp(&ctx->paths[ctx->npaths].re, pattern, REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0) {
        /* Regex compilation failed */
        fprintf(stderr, "Regex compilation failed for pattern: %s\n", pattern);
        return;
    }
#ifdef DEBUG
    else {
        fprintf(stderr, "Registered path for pattern: %s\n", pattern);
    }
#endif

    ctx->paths[ctx->npaths].hook = hook;
    ctx->paths[ctx->npaths].closure = closure;
    ctx->npaths++;
}

/* Perform URL-decoding in-place */
char *
scgi_urldecode(char *input)
{
    char *start = input;
    char *output = input;
    char val;

    do {
        val = *input++;
        if (val == '%') {
            char c;
            val = 0;

            /* Hex-decode the byte value */
            c = *input++;
            if ((c >= '0') && (c <= '9')) val += (c - '0') << 4;
            else if ((c >= 'A') && (c <= 'F')) val += (c - 'A' + 10) << 4;
            else if ((c >= 'a') && (c <= 'f')) val += (c - 'a' + 10) << 4;
            else break; /* Invalid encoding */
            c = *input++;
            if ((c >= '0') && (c <= '9')) val += (c - '0') << 0;
            else if ((c >= 'A') && (c <= 'F')) val += (c - 'A' + 10) << 0;
            else if ((c >= 'a') && (c <= 'f')) val += (c - 'a' + 10) << 0;
            else break; /* Invalid encoding */

            *output++ = val;
        }
        else {
            /* Copy the character as-is */
            *output++ = val;
        }
    } while (val != '\0');

    /* Return the length of the decoded string */
    return start;
}