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
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "scgi.h"
#include "api/cam-rpc.h"

/* Check if a D-Bus method takes arguments */
static int
scgi_method_takes_args(xmlNode *method)
{
    xmlNode *node;
    xmlChar *dir;

    for (node = method->children; node; node = node->next) {
        if (strcmp(node->name, "arg") != 0) continue;
        if ((dir = xmlGetProp(node, "direction")) == NULL) continue;
        if (strcmp(dir, "in") == 0) {
            xmlFree(dir);
            return TRUE;
        }
        xmlFree(dir);
    }
    return FALSE;
}

static void
scgi_parse_calls(struct scgi_ctx *ctx, xmlNode *root, DBusGProxy *proxy)
{
    const char *match = dbus_g_proxy_get_interface(proxy);
    xmlNode *iface;
    xmlNode *node;
    xmlChar *name;

    /* Search for the matching interface */
    for (iface = root->children; iface; iface = iface->next) {
        if (strcmp(iface->name, "interface") != 0) continue;
        if ((name = xmlGetProp(iface, "name")) == NULL) continue;
        if (strcmp(name, match) == 0) {
            xmlFree(name);
            break; /* Found it */
        }
        xmlFree(name);
    }
    if (!iface) return;

    /* Enumerate all methods and signals in the interface */
    for (node = iface->children; node; node = node->next) {
        name = xmlGetProp(node, "name");
        if (name == NULL) continue;

        /* Add signal handlers */
        if (strcmp(node->name, "signal") == 0) {
            fprintf(stderr, "Registering signal handler %s\n", name);
            dbus_g_proxy_add_signal(proxy, name, CAM_DBUS_HASH_MAP, G_TYPE_INVALID);
            dbus_g_proxy_connect_signal(proxy, name, G_CALLBACK(scgi_signal_handler), name, NULL);
            continue; /* Leak the 'name' so that it stays allocated for the signal handler to use. */
        }

        /* Add method handlers */
        if (strcmp(node->name, "method") == 0) {
            fprintf(stderr, "Registering method URI %s\n", name);
            scgi_ctx_register(ctx, name, scgi_method_takes_args(node) ? scgi_call_args : scgi_call_void, proxy);
        }
        xmlFree(name);
    }
}

void
scgi_introspect(struct scgi_ctx *ctx, DBusGConnection* bus, DBusGProxy *proxy)
{
    DBusGProxy *iproxy; /* Proxy for the introspectable interface */
    const char *service = dbus_g_proxy_get_path(proxy);
    GError *error = NULL;
    gboolean okay;
    gchar *result;
    xmlDocPtr doc;
    
    LIBXML_TEST_VERSION

    /* Create a connection to the DBus Introspection interface */
    iproxy = dbus_g_proxy_new_for_name(bus, dbus_g_proxy_get_bus_name(proxy), service, "org.freedesktop.DBus.Introspectable");
    if (iproxy == NULL) {
        fprintf(stderr, "Failed to connect to %s\n", service);
        exit(EXIT_FAILURE);
    }

    /* Execute the D-Bus get call. */
    okay = dbus_g_proxy_call(iproxy, "Introspect", &error, G_TYPE_INVALID, G_TYPE_STRING, &result, G_TYPE_INVALID);
    if (!okay) {
        fprintf(stderr, "Failed to introspect to %s\n", service);
        exit(EXIT_FAILURE);
    }
    g_object_unref(iproxy);

    /* Parse the introspection XML data. */
    doc = xmlReadMemory(result, strlen(result), "noname.xml", NULL, 0);
    if (doc == NULL) {
        fprintf(stderr, "Failed to parse introspection data for %s\n", service);
        exit(EXIT_FAILURE);
    }
    scgi_parse_calls(ctx, xmlDocGetRootElement(doc), proxy);
    xmlFreeDoc(doc);
    g_free(result);
}
