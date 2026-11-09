/* HexChat
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/

/*
 * For Diffie-Hellman key-exchange a 1080bit germain prime is used, the
 * generator g=2 renders a field Fp from 1 to p-1. Therefore breaking it
 * means to solve a discrete logarithm problem with no less than 1080bit.
 *
 * Base64 format is used to send the public keys over IRC.
 *
 * The calculated secret key is hashed with SHA-256, the result is converted
 * to base64 for final use with blowfish.
 */

#include "config.h"
#include "dh1080.h"

#include <openssl/bn.h>
#include <openssl/sha.h>

#include <string.h>
#include <glib.h>

#define DH1080_PRIME_BITS 1080
#define DH1080_PRIME_BYTES 135
#define SHA256_DIGEST_LENGTH 32
#define B64ABC "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#define MEMZERO(x) memset(x, 0x00, sizeof(x))

/* All clients must use the same prime number to be able to keyx */
static const guchar prime1080[DH1080_PRIME_BYTES] =
{
	0xFB, 0xE1, 0x02, 0x2E, 0x23, 0xD2, 0x13, 0xE8, 0xAC, 0xFA, 0x9A, 0xE8, 0xB9, 0xDF, 0xAD, 0xA3, 0xEA,
	0x6B, 0x7A, 0xC7, 0xA7, 0xB7, 0xE9, 0x5A, 0xB5, 0xEB, 0x2D, 0xF8, 0x58, 0x92, 0x1F, 0xEA, 0xDE, 0x95,
	0xE6, 0xAC, 0x7B, 0xE7, 0xDE, 0x6A, 0xDB, 0xAB, 0x8A, 0x78, 0x3E, 0x7A, 0xF7, 0xA7, 0xFA, 0x6A, 0x2B,
	0x7B, 0xEB, 0x1E, 0x72, 0xEA, 0xE2, 0xB7, 0x2F, 0x9F, 0xA2, 0xBF, 0xB2, 0xA2, 0xEF, 0xBE, 0xFA, 0xC8,
	0x68, 0xBA, 0xDB, 0x3E, 0x82, 0x8F, 0xA8, 0xBA, 0xDF, 0xAD, 0xA3, 0xE4, 0xCC, 0x1B, 0xE7, 0xE8, 0xAF,
	0xE8, 0x5E, 0x96, 0x98, 0xA7, 0x83, 0xEB, 0x68, 0xFA, 0x07, 0xA7, 0x7A, 0xB6, 0xAD, 0x7B, 0xEB, 0x61,
	0x8A, 0xCF, 0x9C, 0xA2, 0x89, 0x7E, 0xB2, 0x8A, 0x61, 0x89, 0xEF, 0xA0, 0x7A, 0xB9, 0x9A, 0x8A, 0x7F,
	0xA9, 0xAE, 0x29, 0x9E, 0xFA, 0x7B, 0xA6, 0x6D, 0xEA, 0xFE, 0xFB, 0xEF, 0xBF, 0x0B, 0x7D, 0x8B
};

/* Implemented directly on BIGNUM arithmetic: the low-level DH_* API is
 * deprecated since OpenSSL 3.0 and the DH1080 prime is not a standard group
 * (an EVP/provider DH key for it needs OSSL_PARAM construction that only
 * exists in OpenSSL >= 3.0, while this project still builds against 1.1).
 *
 * Side channels: both modular exponentiations take a SECRET exponent (our
 * private key). BN_mod_exp() is variable-time for such exponents unless the
 * caller sets BN_FLG_CONSTTIME, so we call BN_mod_exp_mont_consttime()
 * directly (valid because p is an odd prime) and also flag the exponent
 * BN_FLG_CONSTTIME. This is what OpenSSL's own DH_generate_key() and
 * DH_compute_key() did for the previous implementation of this file.
 * Secrets live in BN_secure_new() bignums (secure heap when OpenSSL has one,
 * ordinary heap otherwise), are freed with BN_clear_free(), and every
 * temporary buffer holding key material is OPENSSL_cleanse()d.
 */
static BIGNUM *g_p;
static BIGNUM *g_g;

int
dh1080_init (void)
{
	BIGNUM *q = NULL;
	BN_CTX *ctx = NULL;
	int ok = 0;

	g_return_val_if_fail (g_p == NULL, 0);

	g_p = BN_bin2bn (prime1080, DH1080_PRIME_BYTES, NULL);
	g_g = BN_new ();
	q = BN_new ();
	ctx = BN_CTX_new ();

	if (g_p && g_g && q && ctx
	    && BN_is_odd (g_p)
	    && BN_set_word (g_g, 2)
	    && BN_rshift1 (q, g_p)
	    /* p must be a safe prime: p and (p-1)/2 both prime */
	    && BN_check_prime (g_p, ctx, NULL) == 1
	    && BN_check_prime (q, ctx, NULL) == 1)
		ok = 1;

	BN_free (q);
	BN_CTX_free (ctx);
	if (!ok)
		dh1080_deinit ();
	return ok;
}

void
dh1080_deinit (void)
{
	g_clear_pointer (&g_p, BN_free);
	g_clear_pointer (&g_g, BN_free);
}

/* Public key must be in the range 1 < pk < p-1 */
static int
DH_verifyPubKey (const BIGNUM *pk)
{
	BIGNUM *pm1 = BN_dup (g_p);
	int ok = 0;

	if (pm1 && BN_sub_word (pm1, 1))
		ok = !BN_is_negative (pk) && BN_cmp (pk, BN_value_one ()) > 0 && BN_cmp (pk, pm1) < 0;
	BN_free (pm1);
	return ok;
}

/* Longest valid unpadded base64 text for a key of DH1080_PRIME_BYTES bytes
 * (180 chars, or 181 with the trailing 'A' marker used instead of padding). */
#define DH1080_B64_MAX 181

/* Decode into out[] (at least DH1080_B64_MAX + 8 bytes). Returns the decoded
 * length, or 0 if the text is empty, too long or not valid base64. Uses only
 * the caller's fixed-size buffer plus a local one that is cleansed, so secret
 * keys are never left in reallocated heap copies. */
static gsize
dh1080_decode_b64 (const char *data, guchar *out)
{
	char buf[DH1080_B64_MAX + 4];
	gsize len = strlen (data), outlen;
	gint state = 0;
	guint save = 0;

	if (len == 0 || len > DH1080_B64_MAX)
		return 0;

	memcpy (buf, data, len);
	if (len % 4 == 1 && buf[len - 1] == 'A')
		len--;
	while (len % 4 != 0)
		buf[len++] = '=';

	outlen = g_base64_decode_step (buf, len, out, &state, &save);
	OPENSSL_cleanse (buf, sizeof (buf));
	if (outlen > DH1080_PRIME_BYTES)
		return 0;
	return outlen;
}

static char *
dh1080_encode_b64 (const guchar *data, gsize data_len)
{
	/* one allocation, so no stray copy of a private key is left behind */
	char *ret = g_malloc0 (data_len / 3 * 4 + 8);
	gint state = 0;
	guint save = 0;
	gsize n;
	char *p;

	n = g_base64_encode_step (data, data_len, FALSE, ret, &state, &save);
	n += g_base64_encode_close (FALSE, ret + n, &state, &save);
	ret[n] = '\0';
	OPENSSL_cleanse (&save, sizeof (save));

	if ((p = strchr (ret, '=')))
	{
		*p = '\0';
	}
	else
	{
		ret[n] = 'A';
		ret[n + 1] = '\0';
	}

	return ret;
}

void
dh1080_free_secret (void *secret)
{
	if (secret)
	{
		OPENSSL_cleanse (secret, strlen (secret));
		g_free (secret);
	}
}

int
dh1080_generate_key (char **priv_key, char **pub_key)
{
	guchar buf[DH1080_PRIME_BYTES];
	int len, ok = 0;
	BIGNUM *priv = BN_secure_new (), *pub = BN_new (), *range = BN_dup (g_p);
	BN_CTX *ctx = BN_CTX_new ();

	g_assert (priv_key != NULL);
	g_assert (pub_key != NULL);

	/* priv in [2, p-2], pub = g^priv mod p */
	if (priv && pub && range && ctx
	    && BN_sub_word (range, 3)
	    && BN_priv_rand_range (priv, range)
	    && BN_add_word (priv, 2))
	{
		BN_set_flags (priv, BN_FLG_CONSTTIME);
		if (BN_mod_exp_mont_consttime (pub, g_g, priv, g_p, ctx, NULL))
		{
			MEMZERO (buf);
			len = BN_bn2bin (priv, buf);
			*priv_key = dh1080_encode_b64 (buf, len);

			MEMZERO (buf);
			len = BN_bn2bin (pub, buf);
			*pub_key = dh1080_encode_b64 (buf, len);
			ok = 1;
		}
	}

	OPENSSL_cleanse (buf, sizeof (buf));
	BN_clear_free (priv);
	BN_free (pub);
	BN_free (range);
	BN_CTX_free (ctx);
	return ok;
}

int
dh1080_compute_key (const char *priv_key, const char *pub_key, char **secret_key)
{
	guchar pub_data[DH1080_B64_MAX + 8], priv_data[DH1080_B64_MAX + 8];
	gsize pub_len, priv_len;
	BIGNUM *pk = NULL, *priv = BN_secure_new (), *shared = BN_secure_new ();
	BN_CTX *ctx = BN_CTX_new ();
	int ok = 0;

	g_assert (secret_key != NULL);

	/* Verify base64 strings */
	if (strspn (priv_key, B64ABC) != strlen (priv_key)
	    || strspn (pub_key, B64ABC) != strlen (pub_key))
		goto out;

	/* Decoded keys are bounded to DH1080_PRIME_BYTES (and non-empty) */
	pub_len = dh1080_decode_b64 (pub_key, pub_data);
	priv_len = dh1080_decode_b64 (priv_key, priv_data);
	if (pub_len == 0 || priv_len == 0)
		goto out;

	pk = BN_bin2bn (pub_data, pub_len, NULL);
	if (!pk || !priv || !shared || !ctx
	    || !BN_bin2bn (priv_data, priv_len, priv))
		goto out;

	/* private exponent must be in [1, p-2]; 0 or >= p-1 is never ours */
	BN_set_flags (priv, BN_FLG_CONSTTIME);
	if (BN_is_zero (priv) || BN_ucmp (priv, g_p) >= 0)
		goto out;

	if (DH_verifyPubKey (pk)
	    && BN_mod_exp_mont_consttime (shared, pk, priv, g_p, ctx, NULL))
	{
		guchar shared_key[DH1080_PRIME_BYTES] = { 0 };
		guchar sha256[SHA256_DIGEST_LENGTH] = { 0 };
		int shared_len = BN_bn2bin (shared, shared_key);

		SHA256 (shared_key, shared_len, sha256);
		*secret_key = dh1080_encode_b64 (sha256, sizeof(sha256));
		OPENSSL_cleanse (shared_key, sizeof (shared_key));
		OPENSSL_cleanse (sha256, sizeof (sha256));
		ok = 1;
	}

out:
	OPENSSL_cleanse (priv_data, sizeof (priv_data));
	OPENSSL_cleanse (pub_data, sizeof (pub_data));
	BN_free (pk);
	BN_clear_free (priv);
	BN_clear_free (shared);
	BN_CTX_free (ctx);
	return ok;
}
