/*
 * encrypt.c - Manage the global encryptor
 *
 * Copyright (C) 2013 - 2015, Max Lv <max.c.lv@gmail.com>
 *
 * This file is part of the shadowsocks-libev.
 *
 * shadowsocks-libev is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * shadowsocks-libev is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with shadowsocks-libev; see the file COPYING. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#include <openssl/md5.h>
#include <openssl/rand.h>

#define CIPHER_UNSUPPORTED "unsupported"

#include <time.h>
#include <stdio.h>


#include <sodium.h>
#include "ngx_stream_shadowsocks_encrypt.h"

#define OFFSET_ROL(p, o) ((uint64_t)(*(p + o)) << (8 * o))

#define LOGE(...)


static void FATAL(const char *msg)
{
    LOGE("%s", msg);
    exit(-1);
}

static const char * supported_ciphers[CIPHER_NUM] =
{
    "table",
    "rc4",
    "rc4-md5",
    "aes-128-cfb",
    "aes-192-cfb",
    "aes-256-cfb",
    "bf-cfb",
    "camellia-128-cfb",
    "camellia-192-cfb",
    "camellia-256-cfb",
    "cast5-cfb",
    "des-cfb",
    "idea-cfb",
    "rc2-cfb",
    "seed-cfb",
    "salsa20",
    "chacha20"
};

static const int supported_ciphers_iv_size[CIPHER_NUM] =
{
    0, 0, 16, 16, 16, 16, 8, 16, 16, 16, 8, 8, 8, 8, 16, 8, 8
};

static const int supported_ciphers_key_size[CIPHER_NUM] =
{
    0, 16, 16, 16, 24, 32, 16, 16, 24, 32, 16, 8, 16, 16, 16, 32, 32
};

static int crypto_stream_xor_ic(uint8_t *c, const uint8_t *m, uint64_t mlen,
        const uint8_t *n, uint64_t ic, const uint8_t *k,
        int method)
{
    switch (method) {
        case SALSA20:
            return crypto_stream_salsa20_xor_ic(c, m, mlen, n, ic, k);
        case CHACHA20:
            return crypto_stream_chacha20_xor_ic(c, m, mlen, n, ic, k);
    }
    // always return 0
    return 0;
}

static int random_compare(const void *_x, const void *_y, uint32_t i,
        uint64_t a)
{
    uint8_t x = *((uint8_t *)_x);
    uint8_t y = *((uint8_t *)_y);
    return a % (x + i) - a % (y + i);
}

static void merge(uint8_t *left, int llength, uint8_t *right,
        int rlength, uint32_t salt, uint64_t key)
{
    uint8_t *ltmp = (uint8_t *)malloc(llength * sizeof(uint8_t));
    uint8_t *rtmp = (uint8_t *)malloc(rlength * sizeof(uint8_t));

    uint8_t *ll = ltmp;
    uint8_t *rr = rtmp;

    uint8_t *result = left;

    memcpy(ltmp, left, llength * sizeof(uint8_t));
    memcpy(rtmp, right, rlength * sizeof(uint8_t));

    while (llength > 0 && rlength > 0) {
        if (random_compare(ll, rr, salt, key) <= 0) {
            *result = *ll;
            ++ll;
            --llength;
        } else {
            *result = *rr;
            ++rr;
            --rlength;
        }
        ++result;
    }

    if (llength > 0) {
        while (llength > 0) {
            *result = *ll;
            ++result;
            ++ll;
            --llength;
        }
    } else {
        while (rlength > 0) {
            *result = *rr;
            ++result;
            ++rr;
            --rlength;
        }
    }

    free(ltmp);
    free(rtmp);
}

static void merge_sort(uint8_t array[], int length,
        uint32_t salt, uint64_t key)
{
    uint8_t middle;
    uint8_t *left, *right;
    int llength;

    if (length <= 1) {
        return;
    }

    middle = length / 2;

    llength = length - middle;

    left = array;
    right = array + llength;

    merge_sort(left, llength, salt, key);
    merge_sort(right, middle, salt, key);
    merge(left, llength, right, middle, salt, key);
}

static unsigned char *enc_md5(const unsigned char *d, size_t n, unsigned char *md)
{
    return MD5(d, n, md);
}

static void enc_table_init(shadowsocks_t *ss, const char *pass)
{
    uint32_t i;
    uint64_t key = 0;
    uint8_t *digest;

    digest = enc_md5((const uint8_t *)pass, strlen(pass), NULL);

    for (i = 0; i < 8; i++) {
        key += OFFSET_ROL(digest, i);
    }

    for (i = 0; i < 256; ++i) {
        ss->enc_table[i] = i;
    }
    for (i = 1; i < 1024; ++i) {
        merge_sort(ss->enc_table, 256, i, key);
    }
    for (i = 0; i < 256; ++i) {
        // gen decrypt table from encrypt table
        ss->dec_table[ss->enc_table[i]] = i;
    }
}

int cipher_iv_size(const cipher_kt_t *cipher)
{
    return EVP_CIPHER_iv_length(cipher);
}

static int bytes_to_key(const cipher_kt_t *cipher, const digest_type_t *md,
        const uint8_t *pass, uint8_t *key, uint8_t *iv)
{
    size_t datal;
    datal = strlen((const char *)pass);
    return EVP_BytesToKey(cipher, md, NULL, pass, datal, 1, key, iv);
}

static int rand_bytes(uint8_t *output, int len)
{
    return RAND_bytes(output, len);
}

static const cipher_kt_t *get_cipher_type(int method)
{
    if (method <= TABLE || method >= CIPHER_NUM) {
        LOGE("get_cipher_type(): Illegal method");
        return NULL;
    }

    if (method == RC4_MD5) {
        method = RC4;
    }

    if (method >= SALSA20) {
        return NULL;
    }

    const char *ciphername = supported_ciphers[method];
    return EVP_get_cipherbyname(ciphername);
}

static const digest_type_t *get_digest_type(const char *digest)
{
    if (digest == NULL) {
        LOGE("get_digest_type(): Digest name is null");
        return NULL;
    }

    return EVP_get_digestbyname(digest);
}

static void cipher_context_init(shadowsocks_t *ss, cipher_ctx_t *ctx, int method, int enc)
{
    if (method <= TABLE || method >= CIPHER_NUM) {
        LOGE("cipher_context_init(): Illegal method");
        return;
    }

    if (method >= SALSA20) {
        ss->enc_iv_len = supported_ciphers_iv_size[method];
        return;
    }

    const char *ciphername = supported_ciphers[method];
    cipher_evp_t *evp = &ctx->evp;
    const cipher_kt_t *cipher = get_cipher_type(method);
    if (cipher == NULL) {
        LOGE("Cipher %s not found in OpenSSL library", ciphername);
        FATAL("Cannot initialize cipher");
    }
    EVP_CIPHER_CTX_init(evp);
    if (!EVP_CipherInit_ex(evp, cipher, NULL, NULL, NULL, enc)) {
        LOGE("Cannot initialize cipher %s", ciphername);
        exit(EXIT_FAILURE);
    }
    if (!EVP_CIPHER_CTX_set_key_length(evp, ss->enc_key_len)) {
        EVP_CIPHER_CTX_cleanup(evp);
        LOGE("Invalid key length: %d", ss->enc_key_len);
        exit(EXIT_FAILURE);
    }
    if (method > RC4_MD5) {
        EVP_CIPHER_CTX_set_padding(evp, 1);
    }
}

static void cipher_context_set_iv(shadowsocks_t *ss, cipher_ctx_t *ctx,
        uint8_t *iv, size_t iv_len, int enc)
{
    const unsigned char *true_key;

    if (iv == NULL) {
        LOGE("cipher_context_set_iv(): IV is null");
        return;
    }

    if (enc) {
        rand_bytes(iv, iv_len);
    }

    if (ss->enc_method >= SALSA20) {
        memcpy(ctx->iv, iv, iv_len);
        return;
    }

    if (ss->enc_method == RC4_MD5) {
        unsigned char key_iv[32];
        memcpy(key_iv, ss->enc_key, 16);
        memcpy(key_iv + 16, iv, 16);
        true_key = enc_md5(key_iv, 32, NULL);
        iv_len = 0;
    } else {
        true_key = ss->enc_key;
    }

    cipher_evp_t *evp = &ctx->evp;
    if (evp == NULL) {
        LOGE("cipher_context_set_iv(): Cipher context is null");
        return;
    }
    if (!EVP_CipherInit_ex(evp, NULL, NULL, true_key, iv, enc)) {
        EVP_CIPHER_CTX_cleanup(evp);
        FATAL("Cannot set key and IV");
    }
}

void cipher_context_release(shadowsocks_t *ss, cipher_ctx_t *ctx)
{
    if (ss->enc_method >= SALSA20) {
        return;
    }

    cipher_evp_t *evp = &ctx->evp;
    EVP_CIPHER_CTX_cleanup(evp);
}

static int cipher_context_update(cipher_ctx_t *ctx, uint8_t *output, int *olen,
        const uint8_t *input, int ilen)
{
    cipher_evp_t *evp = &ctx->evp;
    return EVP_CipherUpdate(evp, (uint8_t *)output, olen,
            (const uint8_t *)input, (size_t)ilen);
}

char * ss_encrypt(shadowsocks_t *ss, int buf_size, char *plaintext,
        ssize_t *len, struct enc_ctx *ctx)
{
    if (ctx != NULL) {
        static int tmp_len = 0;
        static char *tmp_buf = NULL;

        int err = 1;
        int iv_len = 0;
        int p_len = *len, c_len = *len;
        if (!ctx->init) {
            iv_len = ss->enc_iv_len;
        }

        int buf_len = max(iv_len + c_len, buf_size);
        if (tmp_len < buf_len) {
            tmp_len = buf_len;
            tmp_buf = realloc(tmp_buf, buf_len);
        }
        char *ciphertext = tmp_buf;

        if (!ctx->init) {
            uint8_t iv[MAX_IV_LENGTH];
            cipher_context_set_iv(ss, &ctx->evp, iv, ss->enc_iv_len, 1);
            memcpy(ciphertext, iv, iv_len);
            ctx->counter = 0;
            ctx->init = 1;
        }

        if (ss->enc_method >= SALSA20) {
            int padding = ctx->counter % SODIUM_BLOCK_SIZE;
            if (buf_len < iv_len + padding + c_len) {
                buf_len = max(iv_len + (padding + c_len) * 2, buf_size);
                ciphertext = realloc(ciphertext, buf_len);
                tmp_len = buf_len;
                tmp_buf = ciphertext;
            }
            if (padding) {
                plaintext = realloc(plaintext, max(p_len + padding, buf_size));
                memmove(plaintext + padding, plaintext, p_len);
                memset(plaintext, 0, padding);
            }
            crypto_stream_xor_ic((uint8_t *)(ciphertext + iv_len),
                    (const uint8_t *)plaintext,
                    (uint64_t)(p_len + padding),
                    (const uint8_t *)ctx->evp.iv,
                    ctx->counter / SODIUM_BLOCK_SIZE, ss->enc_key,
                    ss->enc_method);
            ctx->counter += p_len;
            if (padding) {
                memmove(ciphertext + iv_len, ciphertext + iv_len + padding,
                        c_len);
            }
        } else {
            err = cipher_context_update(&ctx->evp,
                    (uint8_t *)(ciphertext + iv_len),
                    &c_len, (const uint8_t *)plaintext,
                    p_len);
            if (!err) {
                free(plaintext);
                return NULL;
            }
        }

        if (*len < iv_len + c_len) {
            plaintext = realloc(plaintext, max(iv_len + c_len, buf_size));
        }
        *len = iv_len + c_len;
        memcpy(plaintext, ciphertext, *len);
        return plaintext;
    } else {
        char *begin = plaintext;
        while (plaintext < begin + *len) {
            *plaintext = (char)ss->enc_table[(uint8_t)*plaintext];
            plaintext++;
        }
        return begin;
    }
}

char * ss_decrypt(shadowsocks_t *ss, int buf_size, char *ciphertext,
        ssize_t *len, struct enc_ctx *ctx)
{
    if (ctx != NULL) {
        static int tmp_len = 0;
        static char *tmp_buf = NULL;

        int c_len = *len, p_len = *len;
        int iv_len = 0;
        int err = 1;
        int buf_len = max(p_len, buf_size);

        if (tmp_len < buf_len) {
            tmp_len = buf_len;
            tmp_buf = realloc(tmp_buf, buf_len);
        }
        char *plaintext = tmp_buf;

        if (!ctx->init) {
            uint8_t iv[MAX_IV_LENGTH];
            iv_len = ss->enc_iv_len;
            p_len -= iv_len;
            memcpy(iv, ciphertext, iv_len);
            cipher_context_set_iv(ss, &ctx->evp, iv, ss->enc_iv_len, 0);
            ctx->counter = 0;
            ctx->init = 1;
        }

        if (ss->enc_method >= SALSA20) {
            int padding = ctx->counter % SODIUM_BLOCK_SIZE;
            if (buf_len < (p_len + padding) * 2) {
                buf_len = max((p_len + padding) * 2, buf_size);
                plaintext = realloc(plaintext, buf_len);
                tmp_len = buf_len;
                tmp_buf = plaintext;
            }
            if (padding) {
                ciphertext =
                    realloc(ciphertext, max(c_len + padding, buf_size));
                memmove(ciphertext + iv_len + padding, ciphertext + iv_len,
                        c_len - iv_len);
                memset(ciphertext + iv_len, 0, padding);
            }
            crypto_stream_xor_ic((uint8_t *)plaintext,
                    (const uint8_t *)(ciphertext + iv_len),
                    (uint64_t)(c_len - iv_len + padding),
                    (const uint8_t *)ctx->evp.iv,
                    ctx->counter / SODIUM_BLOCK_SIZE, ss->enc_key,
                    ss->enc_method);
            ctx->counter += c_len - iv_len;
            if (padding) {
                memmove(plaintext, plaintext + padding, p_len);
            }
        } else {
            err = cipher_context_update(&ctx->evp, (uint8_t *)plaintext, &p_len,
                    (const uint8_t *)(ciphertext + iv_len),
                    c_len - iv_len);
        }

        if (!err) {
            free(ciphertext);
            return NULL;
        }

        if (*len < p_len) {
            ciphertext = realloc(ciphertext, max(p_len, buf_size));
        }
        *len = p_len;
        memcpy(ciphertext, plaintext, *len);
        return ciphertext;
    } else {
        char *begin = ciphertext;
        while (ciphertext < begin + *len) {
            *ciphertext = (char)ss->dec_table[(uint8_t)*ciphertext];
            ciphertext++;
        }
        return begin;
    }
}

void enc_ctx_init(shadowsocks_t *ss, int method, struct enc_ctx *ctx, int enc)
{
    memset(ctx, 0, sizeof(struct enc_ctx));
    cipher_context_init(ss, &ctx->evp, method, enc);
}

static void enc_key_init(shadowsocks_t *ss, int method, const char *pass)
{
    if (method <= TABLE || method >= CIPHER_NUM) {
        LOGE("enc_key_init(): Illegal method");
        return;
    }

    uint8_t iv[MAX_IV_LENGTH];

    cipher_kt_t *cipher;
    cipher_kt_t cipher_info;

    if (method == SALSA20 || method == CHACHA20) {
        // Fake cipher
        cipher = (cipher_kt_t *)&cipher_info;
        cipher->key_len = supported_ciphers_key_size[method];
        cipher->iv_len = supported_ciphers_iv_size[method];
    } else {
        cipher = (cipher_kt_t *)get_cipher_type(method);
    }

    if (cipher == NULL) {
        do {
            LOGE("Cipher %s not found in crypto library",
                    supported_ciphers[method]);
            FATAL("Cannot initialize cipher");
        } while (0);
    }

    const digest_type_t *md = get_digest_type("MD5");
    if (md == NULL) {
        FATAL("MD5 Digest not found in crypto library");
    }

    ss->enc_key_len = bytes_to_key(cipher, md, (const uint8_t *)pass, ss->enc_key, iv);
    if (ss->enc_key_len == 0) {
        FATAL("Cannot generate key and IV");
    }
    if (method == RC4_MD5) {
        ss->enc_iv_len = 16;
    } else {
        ss->enc_iv_len = cipher_iv_size(cipher);
    }
    ss->enc_method = method;
}

int enc_init(shadowsocks_t *ss, const char *pass, const char *method)
{
    int m = TABLE;
    if (method != NULL) {
        for (m = TABLE; m < CIPHER_NUM; m++) {
            if (strcmp(method, supported_ciphers[m]) == 0) {
                break;
            }
        }
        if (m >= CIPHER_NUM) {
            LOGE("Invalid cipher name: %s, use table instead", method);
            m = TABLE;
        }
    }
    if (m == TABLE) {
        enc_table_init(ss, pass);
    } else {
        enc_key_init(ss, m, pass);
    }
    return m;
}


int global_init(void)
{
    /** 如果这两种method，需要初始化sodium
     * method == SALSA20 || method == CHACHA20 **/
    if (sodium_init() == -1) {
        FATAL("Failed to initialize sodium");
    }
    return 0;
}

