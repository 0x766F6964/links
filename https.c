/* https.c
 * HTTPS protocol client implementation
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL.

 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "links.h"

#ifndef PATH_MAX
#define PATH_MAX 255
#endif

#ifndef LINKS_CRT_FILE
#define LINKS_CRT_FILE		links.crt
#endif

#define N_SSL_CONTEXTS	1

static int ssl_initialized = 0;
static SSL_CTX *contexts[N_SSL_CONTEXTS];

#define file_line_arg
#define pass_file_line

static unsigned in_ssl_malloc_hook = 0;

static void *malloc_hook(size_t size file_line_arg)
{
	void *p;
	in_ssl_malloc_hook++;
	if (!size) size = 1;
	do p = malloc(size); while (!p && out_of_memory(0, NULL, 0));
	in_ssl_malloc_hook--;
	return p;
}

static void *realloc_hook(void *ptr, size_t size file_line_arg)
{
	void *p;
	if (!ptr) return malloc_hook(size pass_file_line);
	in_ssl_malloc_hook++;
	if (!size) size = 1;
	do p = realloc(ptr, size); while (!p && out_of_memory(0, NULL, 0));
	in_ssl_malloc_hook--;
	return p;
}

static void free_hook(void *ptr file_line_arg)
{
	if (!ptr) return;
	free(ptr);
}

#define ssl_set_private_paths(c)		(-1)

int ssl_asked_for_password;

static int ssl_password_callback(char *buf, int size, int rwflag, void *userdata)
{
	ssl_asked_for_password = 1;
	if (size > (int)strlen(cast_const_char ssl_options.client_cert_password))
		size = (int)strlen(cast_const_char ssl_options.client_cert_password);
	memcpy(buf, ssl_options.client_cert_password, size);
	return size;
}

links_ssl *getSSL(void)
{
	int idx;
	links_ssl *ssl;
	if (!ssl_initialized) {
		memset(contexts, 0, sizeof contexts);
		CRYPTO_set_mem_functions(malloc_hook, realloc_hook, free_hook);

#if defined(HAVE_RAND_EGD)
		{
			unsigned char f_randfile[PATH_MAX];
			const unsigned char *f = (const unsigned char *)RAND_file_name(cast_char f_randfile, sizeof(f_randfile));
			if (f && RAND_egd(cast_const_char f) < 0) {
				/* Not an EGD, so read and write to it */
				if (RAND_load_file(cast_const_char f_randfile, -1))
					RAND_write_file(cast_const_char f_randfile);
			}
		}
#endif

		{
			unsigned char *os_pool;
			int os_pool_size;
			os_seed_random(&os_pool, &os_pool_size);
			if (os_pool_size) RAND_add(os_pool, os_pool_size, os_pool_size);
			mem_free(os_pool);
		}

		OPENSSL_init_ssl(0, NULL);
		ssl_initialized = 1;
	}

	idx = 0;
	if (!contexts[idx]) {
		SSL_CTX *ctx;
		const SSL_METHOD *m;

		m = SSLv23_client_method();
		if (!m) return NULL;
		contexts[idx] = ctx = SSL_CTX_new((void *)m);
		if (!ctx) return NULL;
#ifndef SSL_OP_NO_COMPRESSION
#define SSL_OP_NO_COMPRESSION	0
#endif
		SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_NO_COMPRESSION);
#ifdef SSL_MODE_ENABLE_PARTIAL_WRITE
		SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
#endif
#ifdef SSL_CTX_set_min_proto_version
#if defined(SSL3_VERSION)
		SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
#elif defined(TLS1_VERSION)
		SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
#elif defined(TLS1_1_VERSION)
		SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION);
#elif defined(TLS1_2_VERSION)
		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#endif
#endif
		if (!idx) {
			if (ssl_set_private_paths(ctx))
				SSL_CTX_set_default_verify_paths(ctx);
		} else {
		}
		SSL_CTX_set_default_passwd_cb(ctx, ssl_password_callback);
	}
	ssl = xmalloc(sizeof(links_ssl));
	if (!ssl)
		return NULL;
	ssl->ctx = contexts[idx];
	ssl->ssl = SSL_new(ssl->ctx);
	clear_ssl_errors(__LINE__);
	if (!ssl->ssl) {
		mem_free(ssl);
		return NULL;
	}
	ssl->bytes_read = ssl->bytes_written = 0;
	ssl->session_set = 0;
	ssl->session_retrieved = 0;
	return ssl;
}

void freeSSL(links_ssl *ssl)
{
	if (!ssl || ssl == DUMMY)
		return;
	{
		int r;
		SSL_set_quiet_shutdown(ssl->ssl, 1);
		r = SSL_shutdown(ssl->ssl);
		if (r < 0)
			clear_ssl_errors(__LINE__);
	}
	SSL_free(ssl->ssl);
	mem_free(ssl);
}

void ssl_finish(void)
{
	int i;
	for (i = 0; i < N_SSL_CONTEXTS; i++) {
		if (contexts[i]) {
			SSL_CTX_free(contexts[i]);
			contexts[i] = NULL;
		}
	}
	if (ssl_initialized) {
		clear_ssl_errors(__LINE__);
#ifdef HAVE_OPENSSL_CLEANUP
		OPENSSL_cleanup();
#endif
		ssl_initialized = 0;
	}
}

void https_func(struct connection *c)
{
	c->ssl = DUMMY;
	http_func(c);
}

static int verify_ssl_host_name(X509 *server_cert, unsigned char *host)
{
	int v;
	unsigned char ipv4_address[4];
	unsigned char ipv6_address[16];

	if (!numeric_ip_address(host, ipv4_address))
		v = X509_check_ip(server_cert, ipv4_address, 4, 0);
	else if (!numeric_ipv6_address(host, ipv6_address, NULL))
		v = X509_check_ip(server_cert, ipv6_address, 16, 0);
	else
		v = X509_check_host(server_cert, cast_const_char host, strlen(cast_const_char host), 0, NULL);

	return v == 1 ? 0 : S_INVALID_CERTIFICATE;
}

int verify_ssl_certificate(links_ssl *ssl, unsigned char *host)
{
	X509 *server_cert;
	int ret;

	if (SSL_get_verify_result(ssl->ssl) != X509_V_OK) {
		clear_ssl_errors(__LINE__);
		return S_INVALID_CERTIFICATE;
	}
	server_cert = SSL_get_peer_certificate(ssl->ssl);
	if (!server_cert) {
		clear_ssl_errors(__LINE__);
		return S_INVALID_CERTIFICATE;
	}
	ret = verify_ssl_host_name(server_cert, host);
	X509_free(server_cert);
	clear_ssl_errors(__LINE__);
	return ret;
}

int verify_ssl_cipher(links_ssl *ssl)
{
	unsigned char *method;
	unsigned char *cipher;
	method = cast_uchar SSL_get_version(ssl->ssl);
	if (!strncmp(cast_const_char method, "SSL", 3))
		return S_INSECURE_CIPHER;
	if (SSL_get_cipher_bits(ssl->ssl, NULL) < 112)
		return S_INSECURE_CIPHER;
	cipher = cast_uchar SSL_get_cipher_name(ssl->ssl);
	if (cipher) {
		if (strstr(cast_const_char cipher, "RC4"))
			return S_INSECURE_CIPHER;
		if (strstr(cast_const_char cipher, "NULL"))
			return S_INSECURE_CIPHER;
	}
	return 0;
}

int ssl_not_reusable(links_ssl *ssl)
{
	unsigned char *cipher;
	if (!ssl || ssl == DUMMY)
		return 0;
	ssl->bytes_read = (ssl->bytes_read + 4095) & ~4095;
	ssl->bytes_written = (ssl->bytes_written + 4095) & ~4095;
	cipher = cast_uchar SSL_get_cipher_name(ssl->ssl);
	if (cipher) {
		if (strstr(cast_const_char cipher, "RC4-") ||
		    strstr(cast_const_char cipher, "DES-") ||
		    strstr(cast_const_char cipher, "RC2-") ||
		    strstr(cast_const_char cipher, "IDEA-") ||
		    strstr(cast_const_char cipher, "GOST-")) {
			return ssl->bytes_read + ssl->bytes_written >= 1 << 20;
		}
	}
	return 0;
}

unsigned char *get_cipher_string(links_ssl *ssl)
{
	unsigned char *version, *cipher;
	unsigned char *s = init_str();
	int l = 0;

	add_num_to_str(&s, &l, SSL_get_cipher_bits(ssl->ssl, NULL));
	add_to_str(&s, &l, cast_uchar "-bit");

	version = cast_uchar SSL_get_version(ssl->ssl);
	if (version) {
		add_chr_to_str(&s, &l, ' ');
		add_to_str(&s, &l, version);
	}
	cipher = cast_uchar SSL_get_cipher_name(ssl->ssl);
	if (cipher) {
		add_chr_to_str(&s, &l, ' ');
		add_to_str(&s, &l, cipher);
	}
	return s;
}

struct session_cache_entry {
	list_entry_1st
	uttime absolute_time;
	SSL_CTX *ctx;
	SSL_SESSION *session;
	int port;
	list_entry_last
	unsigned char host[1];
};

static struct list_head session_cache = { &session_cache, &session_cache };

static struct session_cache_entry *find_session_cache_entry(SSL_CTX *ctx, unsigned char *host, int port)
{
	struct session_cache_entry *sce;
	struct list_head *lsce;
	foreach(struct session_cache_entry, sce, lsce, session_cache)
		if (sce->ctx == ctx && !strcmp(cast_const_char sce->host, cast_const_char host))
			return sce;
	return NULL;
}

SSL_SESSION *get_session_cache_entry(SSL_CTX *ctx, unsigned char *host, int port)
{
	struct session_cache_entry *sce = find_session_cache_entry(ctx, host, port);
	if (!sce)
		return NULL;
	if (get_absolute_time() - sce->absolute_time > SESSION_TIMEOUT)
		return NULL;
	return sce->session;
}

static void set_session_cache_entry(SSL_CTX *ctx, unsigned char *host, int port, SSL_SESSION *s)
{
	struct session_cache_entry *sce = find_session_cache_entry(ctx, host, port);
	size_t sl;
	if (sce) {
		SSL_SESSION_free(sce->session);
		if (s) {
			sce->session = s;
		} else {
			del_from_list(sce);
			mem_free(sce);
		}
		return;
	}
	if (!s)
		return;
	sl = strlen(cast_const_char host);
	if (sl > MAXINT - sizeof(sizeof(struct session_cache_entry))) return;
	sce = xmalloc(sizeof(struct session_cache_entry) + sl);
	sce->absolute_time = get_absolute_time();
	sce->ctx = ctx;
	sce->session = s;
	sce->port = port;
	strcpy(cast_char sce->host, cast_const_char host);
	add_to_list(session_cache, sce);
}

void retrieve_ssl_session(struct connection *c)
{
	if (c->ssl && !c->ssl->session_retrieved && !proxies.only_proxies) {
		SSL_SESSION *s;
		unsigned char *orig_url, *h;
		int p;

		if (c->no_tls /*|| SSL_session_reused(c->ssl->ssl)*/) {
			s = NULL;
			c->ssl->session_retrieved = 1;
		} else {
			s = SSL_get1_session(c->ssl->ssl);
		}
		orig_url = remove_proxy_prefix(c->url);
		h = get_host_name(orig_url);
		p = get_port(orig_url);
		if (s)
			c->ssl->session_retrieved = 1;
		set_session_cache_entry(c->ssl->ctx, h, p, s);
		mem_free(h);
		clear_ssl_errors(__LINE__);
	}
}

static int shrink_session_cache(int u)
{
	uttime now = get_absolute_time();
	struct session_cache_entry *d;
	struct list_head *ld;
	int f = 0;
	if (in_ssl_malloc_hook++)
		goto ret;
	if (u == SH_FREE_SOMETHING && !list_empty(session_cache)) {
		d = list_struct(session_cache.prev, struct session_cache_entry);
		goto delete_last;
	}
	foreach(struct session_cache_entry, d, ld, session_cache) if (u == SH_FREE_ALL || now - d->absolute_time > SESSION_TIMEOUT) {
delete_last:
		ld = d->list_entry.prev;
		del_from_list(d);
		SSL_SESSION_free(d->session);
		mem_free(d);
		f = ST_SOMETHING_FREED;
	}
ret:
	in_ssl_malloc_hook--;
	return f | (list_empty(session_cache) ? ST_CACHE_EMPTY : 0);
}

unsigned long session_info(int type)
{
	switch (type) {
		case CI_FILES:
			return list_size(&session_cache);
		default:
			internal("session_info: bad request");
	}
	return 0;
}

void init_session_cache(void)
{
	register_cache_upcall(shrink_session_cache, 0, cast_uchar "session");
}
