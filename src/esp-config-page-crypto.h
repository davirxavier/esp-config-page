//
// Created by xav on 2/17/26.
//

#ifndef ESP_CONFIG_PAGE_CRYPTO_H
#define ESP_CONFIG_PAGE_CRYPTO_H

#ifndef ESP_CONP_CRYPTO_KEY_ITERATIONS
#define ESP_CONP_CRYPTO_KEY_ITERATIONS 100000
#endif

#define ESP_CONP_CRYPTO_HASH_LEN 32

#ifdef ESP32
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include <mbedtls/md5.h>

#ifdef mbedtls_md5_starts_ret
#define ESP_CONP_MD5_START(ctx) mbedtls_md5_starts_ret(ctx)
#else
#define ESP_CONP_MD5_START(ctx) mbedtls_md5_starts(ctx)
#endif

#ifdef mbedtls_md5_update_ret
#define ESP_CONP_MD5_UPDATE(ctx, data, len) esp_md5_update_ret(ctx, data, len)
#else
#define ESP_CONP_MD5_UPDATE(ctx, data, len) esp_md5_update(ctx, data, len)
#endif

#ifdef mbedtls_md5_finish_ret
#define ESP_CONP_MD5_END(ctx, res) mbedtls_md5_finish_ret(ctx, res)
#else
#define ESP_CONP_MD5_END(ctx, res) mbedtls_md5_finish(ctx, res)
#endif

#define ESP_CONP_MD5_CTX_T mbedtls_md5_context

#elif ESP8266
#include <md5.h>
#include <base64.hpp>
#include <ESP8266TrueRandom.h>
#include <bearssl/bearssl_hmac.h>

#define ESP_CONP_MD5_START(ctx) MD5Init(ctx)
#define ESP_CONP_MD5_UPDATE(ctx, data, len) MD5Update(ctx, data, len)
#define ESP_CONP_MD5_END(ctx, res) MD5Final(res, ctx)
#define ESP_CONP_MD5_CTX_T md5_context_t
#endif

namespace ESP_CONFIG_PAGE_CRYPTO
{
    inline size_t calcBase64StrLen(size_t inputLen)
    {
        return (4 * ((inputLen + 2) / 3)) + 1;
    }

    inline bool base64Encode(const uint8_t *input, const size_t len, char *output)
    {
#ifdef ESP32
        size_t outputLen = 0;
        if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(output), calcBase64StrLen(len), &outputLen, input, len) != 0)
        {
            return false;
        }

        output[outputLen] = 0;
        return true;
#else
        encode_base64(input, len, (unsigned char*) output);
        return true;
#endif
    }

    inline bool base64Decode(const uint8_t *input, const size_t len, uint8_t *output, size_t outputSize, size_t &written)
    {
#ifdef ESP32
        written = 0;
        if (mbedtls_base64_decode(output, outputSize, &written, input, len) != 0)
        {
            return false;
        }

        return true;
#else
        written = decode_base64(input, len, output);
        return true;
#endif
    }

    inline void genRandom(uint8_t *output, const size_t len)
    {
#ifdef ESP32
        esp_fill_random(output, len);
#else
        ESP8266TrueRandom.memfill((char*) output, len);
#endif
    }

    inline bool hmacHash(const uint8_t *key, const size_t keyLen, const uint8_t *data, const size_t dataLen, uint8_t output[ESP_CONP_CRYPTO_HASH_LEN])
    {
#ifdef ESP32
        int res = mbedtls_md_hmac(
            mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
            key,
            keyLen,
            data,
            dataLen,
            output);

        if (res != 0)
        {
            LOGF("Error generating SHA256 hash: 0x%x", -res);
            return false;
        }

        return true;
#else
        br_hmac_key_context hkc;
        br_hmac_key_init(&hkc, &br_sha256_vtable, key, keyLen);

        br_hmac_context hmac;
        br_hmac_init(&hmac, &hkc, ESP_CONP_CRYPTO_HASH_LEN);
        br_hmac_update(&hmac, data, dataLen);
        br_hmac_out(&hmac, output);
        return true;
#endif
    }

    // constant time memcmp
    inline int ct_memcmp(const void *a, const void *b, size_t len)
    {
        const uint8_t *p1 = (const uint8_t *)a;
        const uint8_t *p2 = (const uint8_t *)b;
        uint8_t diff = 0;

        for (size_t i = 0; i < len; i++) {
            diff |= p1[i] ^ p2[i];
        }

        return diff;  // 0 = equal, !=0 = different
    }
}

#endif //ESP_CONFIG_PAGE_CRYPTO_H
