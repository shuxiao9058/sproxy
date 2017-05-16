/*
 * encrypt.h - Define the enryptor's interface
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <openssl/evp.h>

typedef EVP_CIPHER cipher_kt_t;
typedef EVP_CIPHER_CTX cipher_evp_t;
typedef EVP_MD digest_type_t;

#define MAX_KEY_LENGTH EVP_MAX_KEY_LENGTH
#define MAX_IV_LENGTH EVP_MAX_IV_LENGTH
#define MAX_MD_SIZE EVP_MAX_MD_SIZE

typedef struct {
    cipher_evp_t evp;
    uint8_t iv[MAX_IV_LENGTH];
} cipher_ctx_t;

#define SODIUM_BLOCK_SIZE   64
#define CIPHER_NUM          17

#define NONE                -1
#define TABLE               0
#define RC4                 1
#define RC4_MD5             2
#define AES_128_CFB         3
#define AES_192_CFB         4
#define AES_256_CFB         5
#define BF_CFB              6
#define CAMELLIA_128_CFB    7
#define CAMELLIA_192_CFB    8
#define CAMELLIA_256_CFB    9
#define CAST5_CFB           10
#define DES_CFB             11
#define IDEA_CFB            12
#define RC2_CFB             13
#define SEED_CFB            14
#define SALSA20             15
#define CHACHA20            16

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))


/*************************************************************************/
/**   请求相关的结构&函数                                               **/
/*************************************************************************/

/**
 * 每个请求需要两个enc_ctx:
 *       一个用来加密数据
 *       一个用来解密数据
 **/
struct enc_ctx {
    uint8_t init;
    uint64_t counter;
    cipher_ctx_t evp;
};

typedef struct _shadowsocks_s shadowsocks_t;

void enc_ctx_init(shadowsocks_t *ss, int method, struct enc_ctx *ctx, int enc);
void cipher_context_release(shadowsocks_t *ss, cipher_ctx_t *ctx);

char * ss_encrypt(shadowsocks_t *ss, int buf_size, char *plaintext,
        ssize_t *len, struct enc_ctx *ctx);
char * ss_decrypt(shadowsocks_t *ss, int buf_size, char *ciphertext,
        ssize_t *len, struct enc_ctx *ctx);

/*************************************************************************/
/**   Server相关的结构&函数                                             **/
/*************************************************************************/
struct _shadowsocks_s {
    uint8_t enc_table[256];
    uint8_t dec_table[256];
    uint8_t enc_key[MAX_KEY_LENGTH];
    int enc_key_len;
    int enc_iv_len;
    int enc_method;
};

int enc_init(shadowsocks_t *ss, const char *pass, const char *method);

/*************************************************************************/
/**   全局相关的结构&函数                                               **/
/*************************************************************************/
int global_init(void);

