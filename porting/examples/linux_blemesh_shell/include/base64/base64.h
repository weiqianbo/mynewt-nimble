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

/* Calculate the size of base64 encoded data */
#define BASE64_ENCODE_SIZE(len) (((len) + 2) / 3 * 4 + 1)

/*
 * base64_encode - Encode binary data to base64 string
 * @input: Pointer to input binary data
 * @input_len: Length of input data in bytes
 * @output: Buffer for base64 encoded output
 * @output_size: Size of output buffer (unused, for API compatibility)
 * Returns: Length of encoded string (excluding null terminator)
 */
int base64_encode(const uint8_t *input, size_t input_len,
                  char *output, size_t output_size);

/*
 * base64_decode - Decode base64 string to binary data
 * @input: Base64 encoded string
 * @output: Buffer for decoded binary data
 * Returns: Length of decoded data, or -1 on error
 */
int base64_decode(const char *input, uint8_t *output);

#ifdef __cplusplus
}
#endif

#endif /* __BASE64_H__ */
