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

#include "base64/base64.h"

#include <string.h>

static const char base64_enc_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t base64_dec_table[256] = {
    [0 ... 255] = 0xff,
    ['A'] = 0, ['B'] = 1, ['C'] = 2, ['D'] = 3,
    ['E'] = 4, ['F'] = 5, ['G'] = 6, ['H'] = 7,
    ['I'] = 8, ['J'] = 9, ['K'] = 10, ['L'] = 11,
    ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
    ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
    ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25,
    ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29,
    ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33,
    ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37,
    ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
    ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45,
    ['u'] = 46, ['v'] = 47, ['w'] = 48, ['x'] = 49,
    ['y'] = 50, ['z'] = 51,
    ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
    ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
    ['8'] = 60, ['9'] = 61,
    ['+'] = 62, ['/'] = 63,
    ['='] = 0,  /* padding */
};

int base64_encode(const void *data, int length, char *output, int output_width)
{
    const uint8_t *in = (const uint8_t *)data;
    char *out = output;
    int i, j;
    int remaining;
    uint32_t val;

    if (!data || !output || length <= 0) {
        return 0;
    }

    j = 0;
    for (i = 0; i < length; i += 3) {
        remaining = length - i;

        if (remaining >= 3) {
            val = ((uint32_t)in[i] << 16) |
                  ((uint32_t)in[i + 1] << 8) |
                  ((uint32_t)in[i + 2]);

            out[j++] = base64_enc_table[(val >> 18) & 0x3f];
            out[j++] = base64_enc_table[(val >> 12) & 0x3f];
            out[j++] = base64_enc_table[(val >> 6) & 0x3f];
            out[j++] = base64_enc_table[val & 0x3f];
        } else if (remaining == 2) {
            val = ((uint32_t)in[i] << 16) |
                  ((uint32_t)in[i + 1] << 8);

            out[j++] = base64_enc_table[(val >> 18) & 0x3f];
            out[j++] = base64_enc_table[(val >> 12) & 0x3f];
            out[j++] = base64_enc_table[(val >> 6) & 0x3f];
            out[j++] = '=';
        } else {
            val = ((uint32_t)in[i] << 16);

            out[j++] = base64_enc_table[(val >> 18) & 0x3f];
            out[j++] = base64_enc_table[(val >> 12) & 0x3f];
            out[j++] = '=';
            out[j++] = '=';
        }
    }

    out[j] = '\0';
    return j;
}

int base64_decode(const char *input, void *output)
{
    uint8_t *out = (uint8_t *)output;
    const char *in = input;
    int i, j;
    int in_len;
    int pad;
    uint32_t val;
    int result;

    if (!input || !output) {
        return -1;
    }

    in_len = strlen(input);
    if (in_len == 0) {
        return 0;
    }

    /* Remove padding count */
    pad = 0;
    if (in[in_len - 1] == '=') pad++;
    if (in_len > 1 && in[in_len - 2] == '=') pad++;

    j = 0;
    for (i = 0; i < in_len; i += 4) {
        if (i + 4 > in_len) {
            break;
        }

        val = ((uint32_t)base64_dec_table[(uint8_t)in[i]] << 18) |
              ((uint32_t)base64_dec_table[(uint8_t)in[i + 1]] << 12) |
              ((uint32_t)base64_dec_table[(uint8_t)in[i + 2]] << 6) |
              ((uint32_t)base64_dec_table[(uint8_t)in[i + 3]]);

        out[j++] = (uint8_t)(val >> 16);
        out[j++] = (uint8_t)(val >> 8);
        out[j++] = (uint8_t)(val);
    }

    /* Remove padding bytes from output */
    result = j - pad;
    if (result < 0) {
        return -1;
    }

    return result;
}
