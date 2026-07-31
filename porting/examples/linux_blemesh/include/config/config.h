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

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>
#include <stddef.h>
#include "sysinit/sysinit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Conf handler export targets */
enum conf_export_tgt {
    CONF_EXPORT_PERSIST,
    CONF_EXPORT_SHOW,
};

/* Conf handler structure - used to register a config handler */
struct conf_handler {
    const char *ch_name;
    int (*ch_get)(int argc, char **argv, char *val);
    int (*ch_set)(int argc, char **argv, char *val);
    int (*ch_commit)(void);
    int (*ch_export)(void (*func)(char *name, char *val),
                    enum conf_export_tgt tgt);
};

/* Register a conf handler */
int conf_register(struct conf_handler *handler);

/* Load all persisted settings (call ch_set on each registered handler) */
int conf_load(void);

/* Save a single key-value pair to persistent storage */
int conf_save_one(const char *name, const char *value);

/* Get a stored value by name */
int conf_get_stored_value(const char *name, char *value, int value_len);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_H__ */
