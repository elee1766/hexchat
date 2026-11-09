/*
  Copyright (c) 2020 <bakasura@protonmail.ch>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#include <string.h>
#include <glib.h>

#include "fish.h"
#include "utils.h"
#include "dh1080.h"

#include <openssl/bn.h>
#include <openssl/opensslv.h>
#include <openssl/sha.h>

#define DH1080_TEST_PRIME_BITS 1080

/**
 * Auxiliary function: Generate a random string
 * @param out Preallocated string to fill
 * @param len Size of bytes to fill
 */
static void
random_string(char *out, size_t len)
{
    GRand *rand = NULL;
    size_t i = 0;

    rand = g_rand_new();
    for (i = 0; i < len; ++i) {
        out[i] = g_rand_int_range(rand, 1, 256);
    }

    out[len] = 0;

    g_rand_free(rand);
}

/**
 * Check encrypt and decrypt in ECB mode
 */
static void
test_ecb(void)
{
    char *b64 = NULL;
    char *de = NULL;
    int key_len, message_len = 0;
    char key[57];
    char message[1000];

    /* Generate key 32–448 bits (Yes, I start with 8 bits) */
    for (key_len = 1; key_len < 57; ++key_len) {

        random_string(key, key_len);

        for (message_len = 1; message_len < 1000; ++message_len) {
            random_string(message, message_len);

            /* Encrypt */
            b64 = fish_encrypt(key, key_len, message, message_len, FISH_ECB_MODE);
            g_assert_nonnull(b64);

            /* Decrypt */
            /* Linear */
            de = fish_decrypt_str(key, key_len, b64, FISH_ECB_MODE);
			g_assert_cmpstr (de, ==, message);
            g_free(de);
	
            /* Mixed */
            de = fish_decrypt_str(key, key_len, b64, FISH_ECB_MODE);
			g_assert_cmpstr (de, ==, message);
            g_free(de);

            g_free(b64);
        }
    }
}

/**
 * Check encrypt and decrypt in CBC mode
 */
static void
test_cbc(void)
{
    char *b64 = NULL;
    char *de = NULL;
    int key_len, message_len = 0;
    char key[57];
    char message[1000];

    /* Generate key 32–448 bits (Yes, I start with 8 bits) */
    for (key_len = 1; key_len < 57; ++key_len) {

        random_string(key, key_len);

        for (message_len = 1; message_len < 1000; ++message_len) {
            random_string(message, message_len);

            /* Encrypt */
            b64 = fish_encrypt(key, key_len, message, message_len, FISH_CBC_MODE);
            g_assert_nonnull(b64);

            /* Decrypt */
            /* Linear */
            de = fish_decrypt_str(key, key_len, b64, FISH_CBC_MODE);
            g_assert_cmpstr (de, ==, message);
            g_free(de);

            g_free(b64);
        }
    }
}

/**
 * Check the calculation of final length from an encoded string in Base64
 */
static void
test_base64_len (void)
{
    char *b64 = NULL;
    char message[1000];
    int message_end = sizeof (message) - 1;

    random_string(message, message_end);

    for (; message_end >= 0; --message_end) {
        message[message_end] = '\0'; /* Truncate instead of generating new strings */
        b64 = g_base64_encode((const unsigned char *) message, message_end);
        g_assert_nonnull(b64);
        g_assert_cmpuint(strlen(b64), == , base64_len(message_end));
        g_free(b64);
    }
}

/**
 * Check the calculation of final length from an encoded string in BlowcryptBase64
 */
static void
test_base64_fish_len (void)
{
    char *b64 = NULL;
    int message_len = 0;
    char message[1000];

    for (message_len = 1; message_len < 1000; ++message_len) {
        random_string(message, message_len);
        b64 = fish_base64_encode(message, message_len);
        g_assert_nonnull(b64);
        g_assert_cmpuint(strlen(b64), == , base64_fish_len(message_len));
        g_free(b64);
    }
}

/**
 * Check the calculation of final length from an encrypted string in ECB mode
 */
static void
test_base64_ecb_len(void)
{
    char *b64 = NULL;
    int key_len, message_len = 0;
    char key[57];
    char message[1000];

    /* Generate key 32–448 bits (Yes, I start with 8 bits) */
    for (key_len = 1; key_len < 57; ++key_len) {

        random_string(key, key_len);

        for (message_len = 1; message_len < 1000; ++message_len) {
            random_string(message, message_len);
            b64 = fish_encrypt(key, key_len, message, message_len, FISH_ECB_MODE);
            g_assert_nonnull(b64);
            g_assert_cmpuint(strlen(b64), == , ecb_len(message_len));
            g_free(b64);
        }
    }
}

/**
 * Check the calculation of final length from an encrypted string in CBC mode
 */
static void
test_base64_cbc_len(void)
{
    char *b64 = NULL;
    int key_len, message_len = 0;
    char key[57];
    char message[1000];

    /* Generate key 32–448 bits (Yes, I start with 8 bits) */
    for (key_len = 1; key_len < 57; ++key_len) {

        random_string(key, key_len);

        for (message_len = 1; message_len < 1000; ++message_len) {
            random_string(message, message_len);
            b64 = fish_encrypt(key, key_len, message, message_len, FISH_CBC_MODE);
            g_assert_nonnull(b64);
            g_assert_cmpuint(strlen(b64), == , cbc_len(message_len));
            g_free(b64);
        }
    }
}

/**
 * Check the calculation of length limit for a plaintext in each encryption mode
 */
static void
test_max_text_command_len(void)
{
    int max_encoded_len, plaintext_len;
    enum fish_mode mode;

    for (max_encoded_len = 0; max_encoded_len < 10000; ++max_encoded_len) {
        for (mode = FISH_ECB_MODE; mode <= FISH_CBC_MODE; ++mode) {
            plaintext_len = max_text_command_len(max_encoded_len, mode);
            g_assert_cmpuint(encoded_len(plaintext_len, mode), <= , max_encoded_len);
        }
    }
}

/**
 * Check the calculation of length limit for a plaintext in each encryption mode
 */
static void
test_foreach_utf8_data_chunks(void)
{
    GRand *rand = NULL;
    GString *chunks = NULL;
    int  max_chunks_len, chunks_len;
    char ascii_message[1001];
    char *data_chunk = NULL;

    rand = g_rand_new();
    max_chunks_len = g_rand_int_range(rand, 2, 301);
    random_string(ascii_message, 1000);

    data_chunk = ascii_message;

    chunks = g_string_new(NULL);

    while (foreach_utf8_data_chunks(data_chunk, max_chunks_len, &chunks_len)) {
        g_string_append(chunks, g_strndup(data_chunk, chunks_len));
        /* Next chunk */
        data_chunk += chunks_len;
    }
    /* Check data loss */
    g_assert_cmpstr(chunks->str, == , ascii_message);

    g_string_free(chunks, TRUE);
    g_rand_free (rand);
}

/* ---- DH1080 ------------------------------------------------------------ */

/* Independent copy of the DH1080 prime (hex), NOT taken from dh1080.c. */
#define DH1080_TEST_PRIME_HEX \
    "fbe1022e23d213e8acfa9ae8b9dfada3ea6b7ac7a7b7e95ab5eb2df858921feade95e6ac7be7de6adbab8a783e7af7a7fa6a2b7beb1e72eae2b72f9fa2bfb2a2efbefac868badb3e828fa8badfada3e4cc1be7e8afe85e9698a783eb68fa07a77ab6ad7beb618acf9ca2897eb28a6189efa07ab99a8a7fa9ae299efa7ba66deafefbefbf0b7d8b"

/* Known-answer vectors computed with Python big-int pow() and hashlib, i.e.
 * independently of OpenSSL and of dh1080.c. Wire format: unpadded big-endian
 * magnitude, base64 without '=' padding (a trailing 'A' marks "no padding").
 * Vector 2's shared secret has a leading zero byte, which must be hashed
 * unpadded (as the original DH_compute_key based code did). */
static const struct {
    const char *priv_a, *pub_a, *priv_b, *pub_b, *secret;
} dh_kat[] = {
    {
        "A1bsqAFKPfZbFfOOP14Wz2YHy2zAEGHTV/AVRuahY12tgpKJjX3AiLBSDU3YaWtQRXepPu2n8PPcgEv54uIyIoIShhhp+kNQOSJGXoJ+q+dlZsFQrziJYznOnjID3IVhZhxPI6nIJhGDzMlFlE9Z96IMaeEemL2OTQcYzuSQwn4WEtDLwnxzA",
        "TWQ1JXSsr8kXtlNDmTR2LmtmceQal84/+Y244w95GhQWE80tC60URmCUPqHRNnG7qYWzjzu1L1w9zNJbfTXiqtCF40YApDA5hBZD18nVTuu7NgFH3pRm93tW5CCLaJiBJDX6dIW7HXHYgWTbuh5FGv4XqnmTQE+NYt0hS/IhF3cA3gXuZbJAA",
        "Jkat0J+qTjCe1GOoHurNbztVf6iv9OE8HE9X8WYTBy14YidBVM5UKFI6gh+EXbWBowy/2gauGLR4jYOpYqzdDL4r4H0KSzD6eJ8T2RrOronk9YluHOCLDJ+qjxme6p10zkybHfHgOU21jbyL9OWy/S2y+Vd0pIUbG1WT11UNSAD8ogs5STg",
        "3W4g96h3FVLstTl568/s1Xz8kiwE4+Vw5P1iPw0dVSq35AL/dYA08d6iwN9VEXnr/CApb3yzPi5EUDdsxIaVflDfhgrmh75r0xwjaZnN623kpAPlsrQ6JUjiGiT97ujK2LwVchVZ/jFk9Dmew983R6ZNFTixYFQ31IPb5IFbF326RLiero7YA",
        "Wod45Ra7CC89oxRhToH5C9JyGahHT02jXHJKZh2usDc",
    },
    {
        "FK8iis06jBnTZ6BePXYS4grQSBwPua+PIBoljI7pdjiccYlmlqoPryT98sTvc74WlOnPdBTkvImO26/C5Nqz435qMzMo0ca+KGHFC2JB1Mv+OoTT+Y2NFyZoAD6o2cLl2Dd/5lr6y8Mqffe5AGHcop6XaU+dgICXBFBO24fSQIzKJXfyLJDDA",
        "lX8x80ychYGrvzjpWxReCeYycfXLdLGBMoDUbesU+7/EQMKAW89DK3ItAs4/zdLqH7/WsLrapaSzJxPoGPnR30bWiUdaTe3pGZyNaSZsPBqQoJLGk1gtjNuEokqUKjdGh1nG4b4S6h/zNVJr0eRqi9hLWUZBvfHhO+o1sYLMT+VJ4H6FAMuMA",
        "DsNeJIlCgqLHPUvZh+bk0D39qD6lMuwKZu712fSWb+z0/yhF9JNIcEcAelm4pLAnXqvKbDAOtIksac3eh32H0MH6JKFc8yrhHeulHPuGGrJcS1BKsXVCUJAZf6S6+/JcQUsfTLnw3Ph1fyg8ETOd5TdVvIAmb4K18ZyMDKAJf8mPOo2I+WU",
        "thKStalS2i/vmDVXIU2IJWQXdNLAZzlyxGtYoeVeefnk09TCRoRnG0+QdK3fIWSBV/HCLsL1N+GyDmqeQDgSVb72lLwoF4BN0i9pqUG39hFRqRQNBd+ahgtx6NADpW+hpiGppqEvaybVYFzWzYY4H7spOEN+UQWouDK2QqNdFrlC2D5mzjkLA",
        "NRIigJhHZpkpV3CrBhVs31QU0xm+y46ctJ+VIpK5gH0",
    },
};

static void
test_dh1080_kat(void)
{
    char *sec;
    guint i;

    g_assert_true(dh1080_init());
    for (i = 0; i < G_N_ELEMENTS(dh_kat); i++) {
        sec = NULL;
        g_assert_true(dh1080_compute_key(dh_kat[i].priv_a, dh_kat[i].pub_b, &sec));
        g_assert_cmpstr(sec, ==, dh_kat[i].secret);
        g_free(sec);
        sec = NULL;
        g_assert_true(dh1080_compute_key(dh_kat[i].priv_b, dh_kat[i].pub_a, &sec));
        g_assert_cmpstr(sec, ==, dh_kat[i].secret);
        g_free(sec);
    }
    dh1080_deinit();
}

/* Wire encoding implemented here, independently of dh1080.c */
static char *
test_wire_encode(const BIGNUM *bn)
{
    guchar buf[256];
    char *b64, *eq;
    int len = BN_bn2bin(bn, buf);

    b64 = g_base64_encode(buf, len);
    if ((eq = strchr(b64, '='))) {
        *eq = '\0';
        return b64;
    }
    char *r = g_strconcat(b64, "A", NULL);
    g_free(b64);
    return r;
}

static BIGNUM *
test_wire_decode(const char *w)
{
    gsize len = strlen(w), outlen;
    GString *padded;
    guchar *raw;
    BIGNUM *bn;

    if (len % 4 == 1 && w[len - 1] == 'A')
        len--;
    padded = g_string_new_len(w, len);
    while (padded->len % 4)
        g_string_append_c(padded, '=');
    raw = g_base64_decode(padded->str, &outlen);
    bn = BN_bin2bn(raw, outlen, NULL);
    g_free(raw);
    g_string_free(padded, TRUE);
    return bn;
}

static char *
test_expected_secret(const guchar *shared, gsize len)
{
    guchar digest[SHA256_DIGEST_LENGTH];
    char *b64, *eq;

    SHA256(shared, len, digest);
    b64 = g_base64_encode(digest, sizeof digest);
    if ((eq = strchr(b64, '='))) {
        *eq = '\0';
        return b64;
    }
    char *r = g_strconcat(b64, "A", NULL);
    g_free(b64);
    return r;
}

#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>

static EVP_PKEY *
evp_dh_import(const BIGNUM *p, const BIGNUM *g, const BIGNUM *priv, const BIGNUM *pub, int selection)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "DH", NULL);
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM *params;
    EVP_PKEY *pkey = NULL;

    g_assert_nonnull(ctx);
    g_assert_nonnull(bld);
    g_assert_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_FFC_P, p));
    g_assert_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_FFC_G, g));
    if (priv)
        g_assert_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv));
    if (pub)
        g_assert_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PUB_KEY, pub));
    params = OSSL_PARAM_BLD_to_param(bld);
    g_assert_nonnull(params);
    g_assert_true(EVP_PKEY_fromdata_init(ctx) > 0);
    if (EVP_PKEY_fromdata(ctx, &pkey, selection, params) <= 0)
        pkey = NULL;
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

/* Independent shared secret via the provider DH key exchange, unpadded
 * (the DH1080 convention). Returns the derived length or 0 on failure. */
static gsize
evp_dh_derive(EVP_PKEY *own, EVP_PKEY *peer, guchar *out, gsize outsize)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_pkey(NULL, own, NULL);
    unsigned int pad = 0;
    OSSL_PARAM prm[2];
    size_t len = outsize;

    prm[0] = OSSL_PARAM_construct_uint(OSSL_EXCHANGE_PARAM_PAD, &pad);
    prm[1] = OSSL_PARAM_construct_end();
    g_assert_nonnull(ctx);
    g_assert_true(EVP_PKEY_derive_init_ex(ctx, prm) > 0);
    if (EVP_PKEY_derive_set_peer(ctx, peer) <= 0 || EVP_PKEY_derive(ctx, out, &len) <= 0)
        len = 0;
    EVP_PKEY_CTX_free(ctx);
    return len;
}

static EVP_PKEY *
evp_dh_generate(const BIGNUM *p, const BIGNUM *g)
{
    EVP_PKEY *params = evp_dh_import(p, g, NULL, NULL, EVP_PKEY_KEY_PARAMETERS);
    EVP_PKEY_CTX *ctx;
    EVP_PKEY *key = NULL;

    g_assert_nonnull(params);
    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, params, NULL);
    g_assert_true(EVP_PKEY_keygen_init(ctx) > 0);
    g_assert_true(EVP_PKEY_keygen(ctx, &key) > 0);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(params);
    return key;
}

/* Interoperability: keys made by our code and by an independent OpenSSL
 * provider DH implementation must agree in both directions, using the wire
 * format. Many iterations so shared secrets / keys with leading zero bytes
 * (shorter encodings) are exercised too. */
static void
test_dh1080_interop_evp(void)
{
    BIGNUM *p = NULL, *g = BN_new();
    int leading_zero_seen = 0, i;

    g_assert_true(BN_hex2bn(&p, DH1080_TEST_PRIME_HEX) > 0);
    g_assert_true(BN_set_word(g, 2));
    g_assert_true(dh1080_init());

    for (i = 0; i < 300; i++) {
        char *priv_a, *pub_a, *priv_b_w, *pub_b_w, *s1 = NULL, *s2 = NULL, *want;
        BIGNUM *pa, *qa, *pb = NULL, *qb = NULL;
        EVP_PKEY *a_evp, *a_pub_evp, *b_evp, *b_pub_evp;
        guchar d1[256], d2[256];
        gsize l1, l2;

        /* B: independent OpenSSL key pair, put on the wire by test code */
        b_evp = evp_dh_generate(p, g);
        g_assert_true(EVP_PKEY_get_bn_param(b_evp, OSSL_PKEY_PARAM_PRIV_KEY, &pb));
        g_assert_true(EVP_PKEY_get_bn_param(b_evp, OSSL_PKEY_PARAM_PUB_KEY, &qb));
        priv_b_w = test_wire_encode(pb);
        pub_b_w = test_wire_encode(qb);
        b_pub_evp = evp_dh_import(p, g, NULL, qb, EVP_PKEY_PUBLIC_KEY);
        g_assert_nonnull(b_pub_evp);

        /* A: our implementation */
        g_assert_true(dh1080_generate_key(&priv_a, &pub_a));
        pa = test_wire_decode(priv_a);
        qa = test_wire_decode(pub_a);
        g_assert_true(BN_cmp(pa, BN_value_one()) > 0);
        g_assert_true(BN_num_bits(qa) <= DH1080_TEST_PRIME_BITS);
        a_evp = evp_dh_import(p, g, pa, qa, EVP_PKEY_KEYPAIR);
        a_pub_evp = evp_dh_import(p, g, NULL, qa, EVP_PKEY_PUBLIC_KEY);
        g_assert_nonnull(a_evp);   /* our pub validates as a key of ours */
        g_assert_nonnull(a_pub_evp);

        /* EVP derives from both sides; both must agree with each other... */
        l1 = evp_dh_derive(a_evp, b_pub_evp, d1, sizeof d1);
        l2 = evp_dh_derive(b_evp, a_pub_evp, d2, sizeof d2);
        g_assert_cmpuint(l1, >, 0);
        g_assert_cmpuint(l1, ==, l2);
        g_assert_cmpmem(d1, l1, d2, l2);
        if (l1 < 135)
            leading_zero_seen++;

        /* ...and our compute_key must equal SHA-256 of the independent secret */
        want = test_expected_secret(d1, l1);
        g_assert_true(dh1080_compute_key(priv_a, pub_b_w, &s1));
        g_assert_true(dh1080_compute_key(priv_b_w, pub_a, &s2));
        g_assert_cmpstr(s1, ==, want);
        g_assert_cmpstr(s2, ==, want);

        g_free(want); g_free(s1); g_free(s2);
        g_free(priv_a); g_free(pub_a); g_free(priv_b_w); g_free(pub_b_w);
        BN_free(pa); BN_free(qa); BN_free(pb); BN_free(qb);
        EVP_PKEY_free(a_evp); EVP_PKEY_free(a_pub_evp);
        EVP_PKEY_free(b_evp); EVP_PKEY_free(b_pub_evp);
    }
    g_test_message("shared secrets shorter than 135 bytes seen: %d/300", leading_zero_seen);

    BN_free(p);
    BN_free(g);
    dh1080_deinit();
}

/* The independent implementation must reject the same degenerate peer keys */
static void
test_dh1080_evp_rejects_what_we_reject(void)
{
    BIGNUM *p = NULL, *g = BN_new();
    BIGNUM *vals[3];
    EVP_PKEY *own = NULL;
    char *priv, *pub;
    BIGNUM *pn;
    guchar out[256];
    int i;

    g_assert_true(BN_hex2bn(&p, DH1080_TEST_PRIME_HEX) > 0);
    g_assert_true(BN_set_word(g, 2));
    g_assert_true(dh1080_init());
    g_assert_true(dh1080_generate_key(&priv, &pub));
    pn = test_wire_decode(priv);
    {
        BIGNUM *qa = test_wire_decode(pub);
        own = evp_dh_import(p, g, pn, qa, EVP_PKEY_KEYPAIR);
        BN_free(qa);
    }
    g_assert_nonnull(own);

    vals[0] = BN_new(); BN_set_word(vals[0], 1);
    vals[1] = BN_dup(p); BN_sub_word(vals[1], 1);
    vals[2] = BN_dup(p);
    for (i = 0; i < 3; i++) {
        char *w = test_wire_encode(vals[i]), *sec = NULL;
        EVP_PKEY *peer = evp_dh_import(p, g, NULL, vals[i], EVP_PKEY_PUBLIC_KEY);

        g_assert_false(dh1080_compute_key(priv, w, &sec));
        g_assert_null(sec);
        if (peer)
            g_assert_cmpuint(evp_dh_derive(own, peer, out, sizeof out), ==, 0);
        EVP_PKEY_free(peer);
        g_free(w);
        BN_free(vals[i]);
    }
    BN_free(pn);
    BN_free(p);
    BN_free(g);
    EVP_PKEY_free(own);
    g_free(priv); g_free(pub);
    dh1080_deinit();
}

static void
test_dh1080_roundtrip_and_validation(void)
{
    char *priv_a, *pub_a, *priv_b, *pub_b, *sec_a = NULL, *sec_b = NULL, *sec = NULL;
    char longkey[400];

    g_assert_true(dh1080_init());
    g_assert_true(dh1080_generate_key(&priv_a, &pub_a));
    g_assert_true(dh1080_generate_key(&priv_b, &pub_b));
    g_assert_cmpstr(priv_a, !=, priv_b);
    g_assert_true(dh1080_compute_key(priv_a, pub_b, &sec_a));
    g_assert_true(dh1080_compute_key(priv_b, pub_a, &sec_b));
    g_assert_cmpstr(sec_a, ==, sec_b);

    /* invalid public keys: 0, 1, empty, bad alphabet, over-long */
    g_assert_false(dh1080_compute_key(priv_a, "AA", &sec));
    g_assert_false(dh1080_compute_key(priv_a, "AQ", &sec));
    g_assert_false(dh1080_compute_key(priv_a, "", &sec));
    g_assert_false(dh1080_compute_key(priv_a, "!!!!", &sec));
    memset(longkey, 'B', sizeof longkey - 1);
    longkey[sizeof longkey - 1] = '\0';
    g_assert_false(dh1080_compute_key(priv_a, longkey, &sec));
    /* invalid private keys: empty, zero, over-long, bad alphabet */
    g_assert_false(dh1080_compute_key("", pub_b, &sec));
    g_assert_false(dh1080_compute_key("AA", pub_b, &sec));
    g_assert_false(dh1080_compute_key(longkey, pub_b, &sec));
    g_assert_false(dh1080_compute_key("a=b", pub_b, &sec));
    g_assert_null(sec);

    g_free(priv_a); g_free(pub_a); g_free(priv_b); g_free(pub_b);
    g_free(sec_a); g_free(sec_b);
    dh1080_deinit();
}

int
main(int argc, char *argv[]) {

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/fishlim/dh1080/roundtrip_and_validation", test_dh1080_roundtrip_and_validation);
    g_test_add_func("/fishlim/dh1080/known_answer", test_dh1080_kat);
    g_test_add_func("/fishlim/dh1080/interop_openssl_evp", test_dh1080_interop_evp);
    g_test_add_func("/fishlim/dh1080/evp_rejects_degenerate_keys", test_dh1080_evp_rejects_what_we_reject);
    g_test_add_func("/fishlim/ecb", test_ecb);
    g_test_add_func("/fishlim/cbc", test_cbc);
    g_test_add_func("/fishlim/base64_len", test_base64_len);
    g_test_add_func("/fishlim/base64_fish_len", test_base64_fish_len);
    g_test_add_func("/fishlim/base64_ecb_len", test_base64_ecb_len);
    g_test_add_func("/fishlim/base64_cbc_len", test_base64_cbc_len);
    g_test_add_func("/fishlim/max_text_command_len", test_max_text_command_len);
    g_test_add_func("/fishlim/foreach_utf8_data_chunks", test_foreach_utf8_data_chunks);

    fish_init();
    int ret = g_test_run();
    fish_deinit();
    return ret;
}
