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

#ifndef __BASE64_H__
#define __BASE64_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the size of the base64 encoded string for a given binary size */
#define BASE64_ENCODE_SIZE(raw_size) ((((raw_size) + 2) / 3) * 4)

/* Encode binary data to base64 string.
 * Returns the length of the encoded string (excluding null terminator).
 */
int base64_encode(const void *data, int length, char *output, int output_width);

/* Decode a base64 string to binary data.
 * Returns the length of the decoded binary data, or -1 on error.
 */
int base64_decode(const char *input, void *output);

#ifdef __cplusplus
}
#endif

#endif /* __BASE64_H__ */
