/* Local TLS tests for src/common/ssl.c: handshake success, untrusted and
 * expired certificates, and hostname validation, over a socketpair against
 * an in-process OpenSSL server using freshly generated certificates. */

#include "config.h"
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <glib.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include "ssl.h"

typedef struct { X509 *cert; EVP_PKEY *key; } Cred;

static void
add_ext (X509 *cert, X509 *issuer, int nid, const char *value)
{
	X509V3_CTX ctx;
	X509_EXTENSION *ext;

	X509V3_set_ctx_nodb (&ctx);
	X509V3_set_ctx (&ctx, issuer, cert, NULL, NULL, 0);
	ext = X509V3_EXT_conf_nid (NULL, &ctx, nid, value);
	g_assert_nonnull (ext);
	g_assert_true (X509_add_ext (cert, ext, -1));
	X509_EXTENSION_free (ext);
}

/* issuer == NULL creates a self-signed certificate. */
static Cred
make_cred (const char *cn, const char *san, gboolean ca, Cred *issuer, long not_after_secs)
{
	Cred c;
	X509_NAME *name;

	c.key = EVP_EC_gen ("P-256");
	g_assert_nonnull (c.key);
	c.cert = X509_new ();
	X509_set_version (c.cert, 2);
	ASN1_INTEGER_set (X509_get_serialNumber (c.cert), g_random_int_range (1, G_MAXINT));
	X509_gmtime_adj (X509_getm_notBefore (c.cert), -86400 * 30);
	X509_gmtime_adj (X509_getm_notAfter (c.cert), not_after_secs);
	name = X509_get_subject_name (c.cert);
	X509_NAME_add_entry_by_txt (name, "CN", MBSTRING_ASC, (const unsigned char *)cn, -1, -1, 0);
	X509_set_issuer_name (c.cert, issuer ? X509_get_subject_name (issuer->cert) : name);
	X509_set_pubkey (c.cert, c.key);
	add_ext (c.cert, issuer ? issuer->cert : c.cert, NID_basic_constraints,
	         ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
	if (san)
		add_ext (c.cert, issuer ? issuer->cert : c.cert, NID_subject_alt_name, san);
	g_assert_true (X509_sign (c.cert, issuer ? issuer->key : c.key, EVP_sha256 ()) > 0);
	return c;
}

static void
free_cred (Cred *c)
{
	X509_free (c->cert);
	EVP_PKEY_free (c->key);
}

typedef struct { int fd; Cred *cred; Cred *chain; int max_version; } ServerArgs;

static gpointer
server_thread (gpointer data)
{
	ServerArgs *a = data;
	SSL_CTX *ctx = SSL_CTX_new (TLS_server_method ());
	SSL *ssl;

	if (a->max_version)
	{
		/* legacy-protocol server for the minimum-version test */
		SSL_CTX_set_security_level (ctx, 0);
		SSL_CTX_set_max_proto_version (ctx, a->max_version);
	}
	SSL_CTX_use_certificate (ctx, a->cred->cert);
	SSL_CTX_use_PrivateKey (ctx, a->cred->key);
	ssl = SSL_new (ctx);
	SSL_set_fd (ssl, a->fd);
	if (SSL_accept (ssl) > 0)
	{
		SSL_write (ssl, "hello\r\n", 7);
		SSL_shutdown (ssl);
	}
	SSL_free (ssl);
	SSL_CTX_free (ctx);
	return NULL;
}

static int
verify_cb (int ok, X509_STORE_CTX *ctx)
{
	return TRUE;	/* like server.c: result inspected after the handshake */
}

typedef struct
{
	gboolean handshake_ok;
	long verify_result;
	int hostname_result;
	char data[16];
} Result;

static Result
run_connection (Cred *server, Cred *trusted_ca, const char *host)
{
	Result r = { 0 };
	int sv[2];
	ServerArgs args = { 0 };
	GThread *thread;
	SSL_CTX *ctx;
	SSL *ssl;
	X509 *peer;

	g_assert_cmpint (socketpair (AF_UNIX, SOCK_STREAM, 0, sv), ==, 0);
	args.fd = sv[1];
	args.cred = server;
	thread = g_thread_new ("tls-server", server_thread, &args);

	ctx = _SSL_context_init (NULL);
	g_assert_null (_SSL_set_verify (ctx, verify_cb));
	if (trusted_ca)
		g_assert_true (X509_STORE_add_cert (SSL_CTX_get_cert_store (ctx), trusted_ca->cert));
	ssl = _SSL_socket (ctx, sv[0]);
	SSL_set_tlsext_host_name (ssl, host);

	if (SSL_connect (ssl) > 0)
	{
		r.handshake_ok = TRUE;
		r.verify_result = SSL_get_verify_result (ssl);
		peer = _SSL_get_peer_cert (ssl);
		g_assert_nonnull (peer);
		r.hostname_result = _SSL_check_hostname (peer, host);
		X509_free (peer);
		_SSL_recv (ssl, r.data, sizeof (r.data) - 1);
	}
	_SSL_close (ssl);
	_SSL_context_free (ctx);
	close (sv[0]);
	g_thread_join (thread);
	close (sv[1]);
	return r;
}

#define HOST "irc.test.example"

/* Handshake against a server capped at max_version. client_ctx NULL means the
 * HexChat context. Returns whether SSL_connect succeeded. */
static gboolean
handshake_capped (Cred *server, int max_version, gboolean hexchat_ctx)
{
	int sv[2];
	ServerArgs args = { 0 };
	GThread *thread;
	SSL_CTX *ctx;
	SSL *ssl;
	gboolean ok;

	g_assert_cmpint (socketpair (AF_UNIX, SOCK_STREAM, 0, sv), ==, 0);
	args.fd = sv[1];
	args.cred = server;
	args.max_version = max_version;
	thread = g_thread_new ("tls-server", server_thread, &args);

	if (hexchat_ctx)
		ctx = _SSL_context_init (NULL);
	else
	{
		ctx = SSL_CTX_new (TLS_client_method ());
		SSL_CTX_set_min_proto_version (ctx, 0);
	}
	/* Isolate the protocol version from the distro security level so a
	 * failure can only come from the minimum-version setting. */
	SSL_CTX_set_security_level (ctx, 0);
	ssl = SSL_new (ctx);
	SSL_set_fd (ssl, sv[0]);
	SSL_set_connect_state (ssl);
	ok = SSL_connect (ssl) > 0;
	SSL_free (ssl);
	SSL_CTX_free (ctx);
	close (sv[0]);
	g_thread_join (thread);
	close (sv[1]);
	return ok;
}

static void
test_min_tls_version (void)
{
	Cred srv = make_cred (HOST, "DNS:" HOST, FALSE, NULL, 86400 * 30);

	g_assert_cmpint (HEXCHAT_TLS_MIN_VERSION, >=, TLS1_2_VERSION);
	/* Control: the capped TLS 1.1 server works with a permissive client, so a
	 * failure below is due to HexChat's minimum version, not the setup. */
	if (!handshake_capped (&srv, TLS1_1_VERSION, FALSE))
	{
		free_cred (&srv);
		/* g_test_skip needs GLib 2.38; the project's 2.36 floor marks it deprecated */
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		g_test_skip ("OpenSSL build cannot do a TLS 1.1 handshake");
		G_GNUC_END_IGNORE_DEPRECATIONS
		return;
	}
	g_assert_false (handshake_capped (&srv, TLS1_1_VERSION, TRUE));
	g_assert_true (handshake_capped (&srv, TLS1_2_VERSION, TRUE));
	g_assert_true (handshake_capped (&srv, 0, TRUE));
	free_cred (&srv);
}

static void
test_valid (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred (HOST, "DNS:" HOST, FALSE, &ca, 86400 * 30);
	Result r = run_connection (&leaf, &ca, HOST);

	g_assert_true (r.handshake_ok);
	g_assert_cmpint (r.verify_result, ==, X509_V_OK);
	g_assert_cmpint (r.hostname_result, ==, 0);
	g_assert_cmpstr (r.data, ==, "hello\r\n");
	free_cred (&leaf); free_cred (&ca);
}

static void
test_untrusted_ca (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred (HOST, "DNS:" HOST, FALSE, &ca, 86400 * 30);
	Result r = run_connection (&leaf, NULL, HOST);

	g_assert_true (r.handshake_ok);
	/* server sends only the leaf, so the issuer is unknown (20 or 21) */
	g_assert_true (r.verify_result == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY ||
	               r.verify_result == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE);
	free_cred (&leaf); free_cred (&ca);
}

static void
test_self_signed (void)
{
	Cred leaf = make_cred (HOST, "DNS:" HOST, FALSE, NULL, 86400 * 30);
	Result r = run_connection (&leaf, NULL, HOST);

	g_assert_true (r.handshake_ok);
	g_assert_cmpint (r.verify_result, ==, X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT);
	free_cred (&leaf);
}

static void
test_expired (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred (HOST, "DNS:" HOST, FALSE, &ca, -86400);
	Result r = run_connection (&leaf, &ca, HOST);

	g_assert_true (r.handshake_ok);
	g_assert_cmpint (r.verify_result, ==, X509_V_ERR_CERT_HAS_EXPIRED);
	free_cred (&leaf); free_cred (&ca);
}

static void
test_hostname_mismatch (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred ("other.example", "DNS:other.example", FALSE, &ca, 86400 * 30);
	Result r = run_connection (&leaf, &ca, HOST);

	g_assert_true (r.handshake_ok);
	g_assert_cmpint (r.verify_result, ==, X509_V_OK);	/* chain is fine... */
	g_assert_cmpint (r.hostname_result, !=, 0);		/* ...name is not */
	free_cred (&leaf); free_cred (&ca);
}

static void
test_wildcard (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred ("*.test.example", "DNS:*.test.example", FALSE, &ca, 86400 * 30);

	g_assert_cmpint (run_connection (&leaf, &ca, HOST).hostname_result, ==, 0);
	g_assert_cmpint (run_connection (&leaf, &ca, "test.example").hostname_result, !=, 0);
	g_assert_cmpint (run_connection (&leaf, &ca, "a.b.test.example").hostname_result, !=, 0);
	free_cred (&leaf); free_cred (&ca);
}

static void
test_ip_san (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred ("ip", "IP:127.0.0.1", FALSE, &ca, 86400 * 30);

	g_assert_cmpint (run_connection (&leaf, &ca, "127.0.0.1").hostname_result, ==, 0);
	g_assert_cmpint (run_connection (&leaf, &ca, "127.0.0.2").hostname_result, !=, 0);
	free_cred (&leaf); free_cred (&ca);
}

static void
test_cn_fallback (void)
{
	Cred ca = make_cred ("Test CA", NULL, TRUE, NULL, 86400 * 30);
	Cred leaf = make_cred (HOST, NULL, FALSE, &ca, 86400 * 30);

	g_assert_cmpint (run_connection (&leaf, &ca, HOST).hostname_result, ==, 0);
	g_assert_cmpint (run_connection (&leaf, &ca, "evil.example").hostname_result, !=, 0);
	free_cred (&leaf); free_cred (&ca);
}

int
main (int argc, char *argv[])
{
	signal (SIGPIPE, SIG_IGN);	/* as hexchat does; the test server may write to a closed peer */
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/ssl/min-version", test_min_tls_version);
	g_test_add_func ("/ssl/valid", test_valid);
	g_test_add_func ("/ssl/untrusted-ca", test_untrusted_ca);
	g_test_add_func ("/ssl/self-signed", test_self_signed);
	g_test_add_func ("/ssl/expired", test_expired);
	g_test_add_func ("/ssl/hostname-mismatch", test_hostname_mismatch);
	g_test_add_func ("/ssl/wildcard", test_wildcard);
	g_test_add_func ("/ssl/ip-san", test_ip_san);
	g_test_add_func ("/ssl/cn-fallback", test_cn_fallback);
	return g_test_run ();
}
