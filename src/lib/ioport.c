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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "ioport.h"
#include "jsmn.h"

/* Put some upper bounds on the amount of JSON we're willing to parse. */
#define JSON_MAX_TOK    128

/* Recursive helper to get the real size, in tokens, of a non-trivial token. */
static inline int
json_token_size(jsmntok_t *start)
{
    /* Recursively count objects. */
    if (start->type == JSMN_OBJECT) {
        int i;
        jsmntok_t *tok = start+1;
        for (i = 0; i < start->size; i++) {
            tok++;
            tok += json_token_size(tok);
        }
        return tok - start;
    }
    /* Recursively count arrays. */
    if (start->type == JSMN_ARRAY) {
        int i;
        jsmntok_t *tok = start+1;
        for (i = 0; i < start->size; i++) {
            tok += json_token_size(tok);
        }
        return tok - start;
    }
    /* Everything else has size 1. */
    return 1;
}

/* Parse IO port definitions from JSON data. */
struct ioport *
ioport_parse_json(const char *buf, size_t len)
{
    jsmn_parser parser;
    jsmntok_t tokens[JSON_MAX_TOK]; /* Should be enough for any crazy API thing. */
    jsmntok_t *tok;

    /* The parsed ioports. */
    struct ioport *iops;
    char *jsdata;
    int i, count;

    jsmn_init(&parser);
    i = jsmn_parse(&parser, buf, len, tokens, JSON_MAX_TOK);
    if (i < 0) {
        /* Failed to parse the JSON. */
        return NULL;
    }

    /* We expect the contents to be a JSON object. */
    if (tokens[0].type != JSMN_OBJECT) {
        return NULL;
    }
    count = tokens[0].size;
    iops = malloc((count + 1) * sizeof(struct ioport) + len);
    if (!iops) {
        return NULL;
    }
    /* Make a deep copy of the JSON after the iops to store strings. */
    jsdata = memcpy(&iops[count + 1], buf, len);

    /* Parse the tokens making up this JSON object. */
    tok = &tokens[1];
    for (i = 0; i < count; i++) {
        /* To be a valid JSON object, the first token must be a string. */
        if (tok->type != JSMN_STRING) {
            free(iops);
            return NULL;
        }
        jsdata[tok->end] = '\0';
        iops[i].name = jsdata + tok->start;
        tok++;

        /* The following token can be any simple type. */
        jsdata[tok->end] = '\0';
        iops[i].value = jsdata + tok->start;
        tok += json_token_size(tok);

        /* TODO: Parse out primitive types if tok->type == JSMN_PRIMITIVE */
#if 0
        if (tok->type == JSMN_STRING) {
            /* TODO: Deal with escaped UTF-8. This is probably a security hole. */
            cam_dbus_dict_add_printf(h, name, "%s", value);
        }
        else if (tok->type != JSMN_PRIMITIVE) {
            /* Ignore nested types. */
            continue;
        }
        /* Break it down by primitive types. */
        else if (*value == 't') {
            cam_dbus_dict_add_boolean(h, name, 1);
        } else if (*value == 'f') {
            cam_dbus_dict_add_boolean(h, name, 0);
        } else if (*value == 'n') {
            /* TODO: Explicit NULL types and other magic voodoo? */
        }
        /* Below here, we have some kind of numeric type. */
        else if (strcspn(value, ".eE") != (tok->end - tok->start)) {
            char *e;
            double x = strtod(value, &e);
            if (*e != 0) continue; /* TODO: throw an "invalid JSON"? */
            cam_dbus_dict_add_float(h, name, x);
        } else if (*value == '-') {
            /* Use the type 'long long' for negative values. */
            char *e;
            long long x = strtoll(value, &e, 0);
            if (*e != 0) continue; /* TODO: throw an "invalid JSON"? */
            cam_dbus_dict_add_int(h, name, x);
        } else {
            char *e;
            unsigned long long x = strtoull(value, &e, 0);
            if (*e != 0) continue; /* TODO: throw an "invalid JSON"? */
            cam_dbus_dict_add_uint(h, name, x);
        }
#endif
    }
    /* Terminate the list. */
    iops[i].name = NULL;
    iops[i].value = NULL;

    /* Success */
    return iops;
}

/* Load IO port definitions from a JSON file. */
struct ioport *
ioport_load_json(const char *filename)
{
    struct ioport *iops;
    struct stat st;
    void *js;
    int jslen;
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    /* Try to mmap the JSON */
    while ((fstat(fd, &st) == 0) && (st.st_size != 0)) {
        js = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (js == MAP_FAILED) {
            /* Nope - read and parse the old way. */
            break;
        }
        iops = ioport_parse_json(js, st.st_size);
        munmap(js, st.st_size);
        close(fd);
        return iops;
    }

    /* Read and parse the old-fashioned way for streams and sockets.  */
    js = malloc(4096);
    jslen = read(fd, js, 4096);
    close(fd);

    if (jslen <= 0) {
        free(js);
        return NULL;
    }
    iops = ioport_parse_json(js, jslen);
    free(js);
    return iops;
}