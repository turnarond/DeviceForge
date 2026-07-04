/*
* Copyright (c) 2023 ACOINFO CloudNative Team.
* All rights reserved.
*
* Detailed license information can be found in the LICENSE file.
*
* File: lwbase64.cpp base64 encode and decode.
*
* Date: 2023-04-27
*
* Author: Yan.chaodong <yanchaodong@acoinfo.com>
*
*/

#include "lwcomm/lwcomm.h"
#include <string>
#include <cstring>
#include <stdexcept>

/**
 * @brief Base64 encoding table.
 *
 * This table maps 6-bit values to their corresponding Base64 characters.
 */
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

/**
 * @brief Check if a character is a valid Base64 character.
 *
 * @param c The character to check.
 * @return true if the character is a valid Base64 character, false otherwise.
 */
static inline bool is_base64(unsigned char c) 
{
    return (isalnum(c) || (c == '+') || (c == '/'));
}

int LWBase64::base64_encode(const char *in_str, int in_len, std::string &out_str) 
{
    if (!in_str || in_len < 0) {
        return -1;
    }

    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    out_str.clear();
    /* Pre-allocate enough space for the output string. */
    out_str.reserve((in_len + 2) / 3 * 4);

    while (in_len--) {
        char_array_3[i++] = *(in_str++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                out_str += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int k = i; k < 3; k++)
            char_array_3[k] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int k = 0; k < i + 1; k++)
            out_str += base64_chars[char_array_4[k]];

        while (i++ < 3)
            out_str += '=';
    }

    return out_str.length();
}

int LWBase64::base64_decode(const char *in_str, int in_len, std::string &out_str) 
{
    if (!in_str || in_len < 0) {
        return -1;
    }

    int i = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];

    out_str.clear();
    /* Pre-allocate enough space for the output string. */
    out_str.reserve(in_len * 3 / 4);

    while (in_ < in_len) {
        /* Read 4 valid base64 characters or padding. */
        for (i = 0; i < 4 && in_ < in_len; ++i) {
            /* Check if the character is a valid base64 character or padding. */
            if (!is_base64(in_str[in_]) && in_str[in_] != '=') {
                /* Invalid base64 character. */
                return -1;
            }
            
            char_array_4[i] = in_str[in_++];
        }

        /* Calculate the number of padding characters. */
        int padding = 0;
        for (i = 0; i < 4; ++i) {
            if (char_array_4[i] == '=')
                padding++;
        }

        /* Find the position of each character in base64_chars. */  
        for (i = 0; i < 4; ++i) {
            if (char_array_4[i] == '=')
                char_array_4[i] = 0;
            else {
                /* Check if the character is a valid base64 character. */
                if (!is_base64(char_array_4[i])) {
                    /* Invalid base64 character. */
                    return -1;
                }
                
                const char *p = strchr(base64_chars, char_array_4[i]);
                if (p == nullptr) {
                    /* Invalid base64 character. */
                    return -1;
                }
                char_array_4[i] = static_cast<unsigned char>(p - base64_chars);
            }
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

        /* Determine the number of characters to decode based on padding. */
        int decode_len = 3 - padding;
        for (i = 0; i < decode_len; ++i)
            out_str += char_array_3[i];

        /* If padding is greater than 0, it means we have reached the end. */
        if (padding > 0)
            break;
    }

    return out_str.length();
}