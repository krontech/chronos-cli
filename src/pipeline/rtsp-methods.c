
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

void
rtsp_method_options(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len)
{
    rtsp_start_response(conn, 200, "OK");
    rtsp_write_header(conn, "Public: DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE");
    rtsp_write_header(conn, "");
}

void
rtsp_method_describe(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len)
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    rtsp_start_response(conn, 200, "OK");
    rtsp_write_header(conn, "Content-Type: application/sdp");
    rtsp_write_header(conn, "");

    rtsp_write_payload(conn, "v=0");
    getsockname(conn->sock, (struct sockaddr*)&addr, &addrlen);
    if (addr.ss_family == AF_INET) {
        char tmp[INET_ADDRSTRLEN];
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr;
        rtsp_write_payload(conn, "c=IN IP4 %s", inet_ntop(AF_INET, &sin->sin_addr, tmp, sizeof(tmp)));
    }
    else if (addr.ss_family == AF_INET6) {
        char tmp[INET6_ADDRSTRLEN];
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr;
        rtsp_write_payload(conn, "c=IN IP6 %s", inet_ntop(AF_INET6, &sin6->sin6_addr, tmp, sizeof(tmp)));
    }
    rtsp_write_payload(conn, "m=video 5000 RTP/AVP 96");
    rtsp_write_payload(conn, "a=rtpmap:96 H264/90000");
    rtsp_finish_payload(conn);
}

void
rtsp_method_setup(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len)
{
    char host[INET6_ADDRSTRLEN];
    const char *transport = rtsp_header_find(conn, "Transport");
    unsigned int port = 5000;
    
    /* We don't support merging of streams. */
    if (rtsp_header_find(conn, "Session")) {
        rtsp_client_error(conn, 459, "Aggregate Operation Now Allowed");
        return;
    }

    /* Check for a preferred client port. */
    if (transport) {
        char pvalue[32];
        if (rtsp_param_find(transport, "client_port", pvalue, sizeof(pvalue))) {
            char *portend;
            unsigned int lowport = strtoul(pvalue, &portend, 0);
            unsigned int highport = lowport;
            /* Check for an optional port range. */
            if (*portend == '-') {
                highport = strtoul(portend+1, &portend, 0);
            }
            /* If we got valid ports, use one from the client's range. */
            if ((*portend == '\0') && (lowport != 0) && (highport >= lowport)) {
                port = lowport + (rand() % (highport - lowport));
            }
        }
    }

    /* If an existing session is in progress, clear it. */
    if (ctx->session_state != RTSP_SESSION_TEARDOWN) {
        ctx->session_state = RTSP_SESSION_TEARDOWN;
        rtsp_server_run_hook(ctx);
    }

    /* Generate a new session ID. */
    ctx->session_id = rand();
    memset(&ctx->dest, 0, sizeof(ctx->dest));
    if (conn->addr.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ctx->dest;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(port);
        sin->sin_addr = ((struct sockaddr_in *)&conn->addr)->sin_addr;
        inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host));
    }
    else {
        rtsp_client_error(conn, 400, "Bad Request");
        return;
    }

    rtsp_start_response(conn, 200, "OK");
    rtsp_write_header(conn, "Session: %d", ctx->session_id);
    rtsp_write_header(conn, "Transport: RTP/AVP;unicast;client_port=%d;server_port=5000;host=%s", port, host);
    rtsp_write_header(conn, "");

    /* The session is now in the SETUP state. */
    ctx->session_state = RTSP_SESSION_SETUP;
    rtsp_server_run_hook(ctx);
}

void
rtsp_method_play(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len)
{
    /* HACK: The worlds laziest implementation. */
    rtsp_start_response(conn, 200, "OK");
    rtsp_write_header(conn, "");

    /* The session is now in the PLAY state. */
    if (ctx->session_state != RTSP_SESSION_PLAY) {
        ctx->session_state = RTSP_SESSION_PLAY;
        rtsp_server_run_hook(ctx);
    }
}

void
rtsp_method_pause(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len)
{
    /* HACK: The worlds laziest implementation. */
    rtsp_start_response(conn, 200, "OK");
    rtsp_write_header(conn, "");

    /* The session is now in the PLAY state. */
    if (ctx->session_state != RTSP_SESSION_PAUSE) {
        ctx->session_state = RTSP_SESSION_PAUSE;
        rtsp_server_run_hook(ctx);
    }
}

void
rtsp_method_teardown(struct rtsp_ctx *ctx, struct rtsp_conn *conn, const char *payload, size_t len)
{
    /* HACK: The worlds laziest implementation. */
    rtsp_start_response(conn, 200, "OK");
    rtsp_write_header(conn, "");

    /* The session is now in the PLAY state. */
    if (ctx->session_state != RTSP_SESSION_TEARDOWN) {
        ctx->session_state = RTSP_SESSION_TEARDOWN;
        rtsp_server_run_hook(ctx);
    }
}