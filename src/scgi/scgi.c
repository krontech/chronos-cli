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

/* Helper to request more data from an SCGI connection before continuing. */
static int
scgi_want_more(struct scgi_conn *conn)
{
    /* Request too large */
    if (conn->rx.length >= sizeof(conn->rx.buffer)) {
        return -1;
    }

    /* Start another async read */
    g_input_stream_read_async(conn->istream, 
                            conn->rx.buffer, sizeof(conn->rx.buffer) - conn->rx.length,
                            G_PRIORITY_DEFAULT,
                            NULL,
                            scgi_data_ready,
                            conn);
    
    return 0;
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
        conn->netstrlen = strtoul(conn->rx.buffer, &end, 10);
        if ((conn->netstrlen == 0) || (end != sep)) {
            /* Not a valid netstring */
            return -1;
        }

        /* Continue parsing the request headers */
        conn->rx.offset = (end - conn->rx.buffer) + 1;
        conn->state = SCGI_STATE_REQ_HEADERS;
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

            /* Process the next header */
            name = end;
        }
        conn->rx.offset = (netstrend - conn->rx.buffer) + 1;

        /* Continue parsing the request body, if present */
        conn->state = SCGI_STATE_REQ_BODY;
    }

    if (conn->state == SCGI_STATE_REQ_BODY) {
        /* Do nothing until we receive the body. */
        if (conn->rx.length < (conn->rx.offset + conn->contentlen)) {
            return scgi_want_more(conn);
        }

        /* Handle the SCGI request */
        conn->state = SCGI_STATE_RESPONSE;
        if (conn->ctx->hook) {
            conn->ctx->hook(conn, conn->ctx->closure);
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

    /* Free the resources */
    g_object_unref(conn->sock);
    g_free(conn);
}

static void
scgi_write_done(GObject *obj, GAsyncResult *result, gpointer user_data)
{
    struct scgi_conn *conn = user_data;
    GError *error;
    int count = g_output_stream_write_finish(G_OUTPUT_STREAM(obj), result, &error);
    
    /* Check for errors */
    if (count < 0) {
        scgi_conn_destroy(conn);
        return;
    }

    conn->tx.offset += count;
    if (conn->tx.offset < conn->tx.length) {
        /* There is more data to be sent */
        g_output_stream_write_async(G_OUTPUT_STREAM(obj),
                                    conn->tx.buffer + conn->tx.offset,
                                    conn->tx.length - conn->tx.offset,
                                    G_PRIORITY_DEFAULT,
                                    NULL,
                                    scgi_write_done,
                                    user_data);
    }
    else if (conn->state == SCGI_STATE_RESPONSE) {
        /* Response has been sent - clenaup */
        g_io_stream_close(G_IO_STREAM(conn->sock), NULL, &error);
        scgi_conn_destroy(conn);
    }
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

    /* Process the request data. */
    conn->rx.length += count;
    if (conn->rx.length >= sizeof(conn->rx.buffer)) {
        /* Request too large */
        scgi_conn_destroy(conn);
        return;
    }
    conn->rx.buffer[conn->rx.length] = '\0';

    if (scgi_process_data(conn) < 0) {
        scgi_conn_destroy(conn);
        return;
    }

    /* If there is data to be sent - send it. */
    if (conn->tx.offset < conn->tx.length) {
        g_output_stream_write_async(conn->ostream,
                                    conn->tx.buffer + conn->tx.offset,
                                    conn->tx.length - conn->tx.offset,
                                    G_PRIORITY_DEFAULT,
                                    NULL,
                                    scgi_write_done,
                                    user_data);
    }
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
        hook(conn, closure);

        /* If data is ready to be written, write it */
        if (conn->tx.offset >= conn->tx.length) continue;
        g_output_stream_write_async(conn->ostream,
                                    conn->tx.buffer + conn->tx.offset,
                                    conn->tx.length - conn->tx.offset,
                                    G_PRIORITY_DEFAULT,
                                    NULL,
                                    scgi_write_done,
                                    conn);
    }
}
