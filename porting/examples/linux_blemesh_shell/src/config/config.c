/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "config/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONF_MAX_NAME_LEN    128
#define CONF_MAX_VAL_LEN     1024
#define CONF_MAX_HANDLERS    32
#define CONF_MAX_ENTRIES     64
#define CONF_STORAGE_FILE    "/tmp/bt_mesh_settings.conf"

/* Simple linked list of conf entries */
struct conf_entry {
    char name[CONF_MAX_NAME_LEN];
    char val[CONF_MAX_VAL_LEN];
};

static struct conf_handler *handlers[CONF_MAX_HANDLERS];
static int num_handlers;

static struct conf_entry entries[CONF_MAX_ENTRIES];
static int num_entries;

static int conf_save_to_file(void)
{
    // FILE *f;
    // int i;

    // f = fopen(CONF_STORAGE_FILE, "w");
    // if (!f) {
    //     return -1;
    // }

    // for (i = 0; i < num_entries; i++) {
    //     fprintf(f, "%s=%s\n", entries[i].name, entries[i].val);
    // }

    // fclose(f);
    return 0;
}

int conf_register(struct conf_handler *handler)
{
    if (!handler || !handler->ch_name) {
        return -1;
    }

    if (num_handlers >= CONF_MAX_HANDLERS) {
        return -1;
    }

    handlers[num_handlers++] = handler;
    return 0;
}

int conf_save_one(const char *name, const char *value)
{
    int i;

    if (!name) {
        return -1;
    }

    /* Check if entry already exists */
    for (i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            if (value) {
                strncpy(entries[i].val, value, CONF_MAX_VAL_LEN - 1);
                entries[i].val[CONF_MAX_VAL_LEN - 1] = '\0';
            } else {
                /* Delete entry */
                memmove(&entries[i], &entries[i + 1],
                        (num_entries - i - 1) * sizeof(struct conf_entry));
                num_entries--;
            }
            return conf_save_to_file();
        }
    }

    /* Add new entry */
    if (num_entries >= CONF_MAX_ENTRIES) {
        return -1;
    }

    strncpy(entries[num_entries].name, name, CONF_MAX_NAME_LEN - 1);
    entries[num_entries].name[CONF_MAX_NAME_LEN - 1] = '\0';

    if (value) {
        strncpy(entries[num_entries].val, value, CONF_MAX_VAL_LEN - 1);
        entries[num_entries].val[CONF_MAX_VAL_LEN - 1] = '\0';
    } else {
        entries[num_entries].val[0] = '\0';
    }

    num_entries++;
    return conf_save_to_file();
}

int conf_get_stored_value(const char *name, char *value, int value_len)
{
    int i;

    if (!name || !value) {
        return -1;
    }

    for (i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            strncpy(value, entries[i].val, value_len - 1);
            value[value_len - 1] = '\0';
            return 0;
        }
    }

    return -1;
}

int conf_load(void)
{
    FILE *f;
    char line[CONF_MAX_NAME_LEN + CONF_MAX_VAL_LEN + 2];
    char *eq;
    char *name;
    char *val;
    int i, j;
    int argc;
    char *argv[8];
    int rc;

    /* First, load from file */
    f = fopen(CONF_STORAGE_FILE, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            /* Remove trailing newline */
            line[strcspn(line, "\r\n")] = '\0';

            eq = strchr(line, '=');
            if (!eq) {
                continue;
            }

            *eq = '\0';
            name = line;
            val = eq + 1;

            /* Store in memory */
            if (num_entries < CONF_MAX_ENTRIES) {
                strncpy(entries[num_entries].name, name, CONF_MAX_NAME_LEN - 1);
                entries[num_entries].name[CONF_MAX_NAME_LEN - 1] = '\0';
                strncpy(entries[num_entries].val, val, CONF_MAX_VAL_LEN - 1);
                entries[num_entries].val[CONF_MAX_VAL_LEN - 1] = '\0';
                num_entries++;
            }

            /* Find the handler for this name and call ch_set */
            for (i = 0; i < num_handlers; i++) {
                if (strncmp(name, handlers[i]->ch_name,
                            strlen(handlers[i]->ch_name)) == 0) {
                    if (handlers[i]->ch_set) {
                        /* Parse sub-keys: split name by '/' */
                        char name_copy[CONF_MAX_NAME_LEN];
                        strncpy(name_copy, name, CONF_MAX_NAME_LEN - 1);
                        name_copy[CONF_MAX_NAME_LEN - 1] = '\0';

                        argc = 0;
                        argv[argc++] = name_copy;

                        for (j = 0; j < (int)strlen(name_copy) && argc < 8; j++) {
                            if (name_copy[j] == '/') {
                                name_copy[j] = '\0';
                                argv[argc++] = &name_copy[j + 1];
                            }
                        }

                        rc = handlers[i]->ch_set(argc, argv, val);
                        /* Continue loading even if one handler fails */
                    }
                    break;
                }
            }
        }
        fclose(f);
    }

    /* Call ch_commit on all handlers */
    for (i = 0; i < num_handlers; i++) {
        if (handlers[i]->ch_commit) {
            handlers[i]->ch_commit();
        }
    }

    return 0;
}
