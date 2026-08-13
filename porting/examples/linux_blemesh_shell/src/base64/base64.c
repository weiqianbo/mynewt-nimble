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

static const uint8_t base64_dec_table[128] = {
    ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,
    ['E'] = 4,  ['F'] = 5,  ['G'] = 6,  ['H'] = 7,
    ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11,
    ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
    ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
    ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27,
    ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31,
    ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35,
    ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39,
    ['o'] = 40, ['p'] = 41, ['q'] = 42, ['r'] = 43,
    ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
    ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51,
    ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
    ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
    ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63,
};

int base64_encode(const uint8_t *input, size_t input_len,
                  char *output, size_t output_size)
{
    size_t i, j;
    size_t required_len;

    if (input == NULL || output == NULL || input_len == 0) {
        return 0;
    }

    required_len = 4 * ((input_len + 2) / 3) + 1;
    if (output_size < required_len) {
        return -1;
    }

    for (i = 0, j = 0; i < input_len; i += 3) {
        uint32_t octet_a = input[i];
        uint32_t octet_b = (i + 1 < input_len) ? input[i + 1] : 0;
        uint32_t octet_c = (i + 2 < input_len) ? input[i + 2] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = base64_enc_table[(triple >> 18) & 0x3F];
        output[j++] = base64_enc_table[(triple >> 12) & 0x3F];
        output[j++] = base64_enc_table[(triple >> 6) & 0x3F];
        output[j++] = base64_enc_table[triple & 0x3F];
    }

    if (input_len % 3 == 1) {
        output[required_len - 2] = '=';
    } else if (input_len % 3 == 2) {
        output[required_len - 1] = '=';
    }

    output[required_len - 1] = '\0';
    return (int)(required_len - 1);
}

int base64_decode(const char *input, uint8_t *output)
{
    size_t i, j;
    size_t input_len;
    size_t required_len;
    int pad = 0;

    if (input == NULL || output == NULL) {
        return 0;
    }

    input_len = strlen(input);
    if (input_len == 0) {
        return 0;
    }

    if (input[input_len - 1] == '=') pad++;
    if (input_len > 1 && input[input_len - 2] == '=') pad++;

    required_len = (input_len / 4) * 3 - pad;

    for (i = 0, j = 0; i < input_len;) {
        uint32_t sextet_a = input[i] < 128 ? base64_dec_table[input[i]] : 0;
        uint32_t sextet_b = input[i + 1] < 128 ? base64_dec_table[input[i + 1]] : 0;
        uint32_t sextet_c = (i + 2 < input_len && input[i + 2] < 128) ?
                            base64_dec_table[input[i + 2]] : 0;
        uint32_t sextet_d = (i + 3 < input_len && input[i + 3] < 128) ?
                            base64_dec_table[input[i + 3]] : 0;

        uint32_t triple = (sextet_a << 18) | (sextet_b << 12) |
                          (sextet_c << 6) | sextet_d;

        if (j < required_len) output[j++] = (triple >> 16) & 0xFF;
        if (j < required_len) output[j++] = (triple >> 8) & 0xFF;
        if (j < required_len) output[j++] = triple & 0xFF;

        i += 4;
    }

    return (int)required_len;
}
