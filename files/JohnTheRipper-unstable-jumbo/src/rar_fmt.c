/* RAR 3.x cracker patch for JtR. Hacked together during
 * April of 2011 by Dhiru Kholia <dhiru.kholia at gmail.com> for GSoC.
 * magnum added -p mode support, using code based on libclamav
 * and OMP, AES-NI and OpenCL support.
 *
 * This software is Copyright (c) 2011, Dhiru Kholia <dhiru.kholia at gmail.com>
 * and Copyright (c) 2012, magnum and it is hereby released to the general public
 * under the following terms:
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * This code is based on the work of Alexander L. Roshal (C)
 *
 * The unRAR sources may be used in any software to handle RAR
 * archives without limitations free of charge, but cannot be used
 * to re-create the RAR compression algorithm, which is proprietary.
 * Distribution of modified unRAR sources in separate form or as a
 * part of other software is permitted, provided that it is clearly
 * stated in the documentation and source comments that the code may
 * not be used to develop a RAR (WinRAR) compatible archiver.
 *
 * Huge thanks to Marc Bevand <m.bevand (at) gmail.com> for releasing unrarhp
 * (http://www.zorinaq.com/unrarhp/) and documenting the RAR encryption scheme.
 * This patch is made possible by unrarhp's documentation.
 *
 * http://anrieff.net/ucbench/technical_qna.html is another useful reference
 * for RAR encryption scheme.
 *
 * Thanks also to Pavel Semjanov for crucial help with Huffman table checks.
 *
 * For type = 0 for files encrypted with "rar -hp ..." option
 * archive_name:$RAR3$*type*hex(salt)*hex(partial-file-contents):type::::archive_name
 *
 * For type = 1 for files encrypted with "rar -p ..." option
 * archive_name:$RAR3$*type*hex(salt)*hex(crc)*PACK_SIZE*UNP_SIZE*archive_name*offset-for-ciphertext*method:type::file_name
 *
 * or (inlined binary)
 *
 * archive_name:$RAR3$*type*hex(salt)*hex(crc)*PACK_SIZE*UNP_SIZE*1*hex(full encrypted file)*method:type::file_name
 *
 */

#include "arch.h"
#include <openssl/engine.h>
#include <openssl/evp.h>
#if defined(__APPLE__) && defined(__MACH__)
#ifdef __MAC_OS_X_VERSION_MIN_REQUIRED
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
#define COMMON_DIGEST_FOR_OPENSSL
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif
#else
#include <openssl/sha.h>
#endif
#else
#include <openssl/sha.h>
#endif

#include <openssl/ssl.h>
#undef MEM_FREE

#include <string.h>
#include <assert.h>
#include <errno.h>
#include "crc32.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#include "unicode.h"
#include "johnswap.h"
#include "unrar.h"
#include "config.h"

#define FORMAT_LABEL		"rar"
#define FORMAT_NAME		"RAR3 SHA-1 AES"
#define ALGORITHM_NAME		"32/" ARCH_BITS_STR

#ifdef DEBUG
#define BENCHMARK_COMMENT	" (1-16 characters)"
#else
#define BENCHMARK_COMMENT	" (4 characters)"
#endif
#define BENCHMARK_LENGTH	-1

#define PLAINTEXT_LENGTH	16
#define UNICODE_LENGTH		(2 * PLAINTEXT_LENGTH)
#define BINARY_SIZE		0
#define SALT_SIZE		sizeof(rarfile)
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	1

#define ROUNDS			0x40000

#define MIN(a, b)		(((a) > (b)) ? (b) : (a))
#define MAX(a, b)		(((a) > (b)) ? (a) : (b))

/* The reason we want to bump OMP_SCALE in this case is to even out the
   difference in processing time for different length keys. It doesn't
   boost performance in other ways */
#ifdef _OPENMP
#include <omp.h>
#include <pthread.h>
#define OMP_SCALE		4
static pthread_mutex_t *lockarray;
#endif

static int omp_t = 1;
static unsigned char *saved_salt;
static unsigned char *saved_key;
static int (*cracked);
static unpack_data_t (*unpack_data);

static unsigned int *saved_len;
static unsigned char *aes_key;
static unsigned char *aes_iv;

typedef struct {
	unsigned char salt[8];
	int type;	/* 0 = -hp, 1 = -p */
	unsigned char *raw_data;
	/* for rar -p mode only: */
	union {
		unsigned int w;
		unsigned char c[4];
	} crc;
	unsigned long long pack_size;
	unsigned long long unp_size;
	char *archive_name;
	long pos;
	int method;
} rarfile;

static rarfile *cur_file;

/* cRARk use 4-char passwords for CPU benchmark */
static struct fmt_tests cpu_tests[] = {
	{"$RAR3$*0*b109105f5fe0b899*d4f96690b1a8fe1f120b0290a85a2121", "test"},
	{"$RAR3$*0*42ff7e92f24fb2f8*9d8516c8c847f1b941a0feef064aaf0d", "1234"},
	{"$RAR3$*0*56ce6de6ddee17fb*4c957e533e00b0e18dfad6accc490ad9", "john"},
	/* -p mode tests, -m0 and -m3 (in that order) */
	{"$RAR3$*1*c47c5bef0bbd1e98*965f1453*48*47*1*c5e987f81d316d9dcfdb6a1b27105ce63fca2c594da5aa2f6fdf2f65f50f0d66314f8a09da875ae19d6c15636b65c815*30", "test"},
	{"$RAR3$*1*b4eee1a48dc95d12*965f1453*64*47*1*0fe529478798c0960dd88a38a05451f9559e15f0cf20b4cac58260b0e5b56699d5871bdcc35bee099cc131eb35b9a116adaedf5ecc26b1c09cadf5185b3092e6*33", "test"},
#ifdef DEBUG
	/* Various lengths, these should be in self-test but not benchmark */
	/* from CMIYC 2012 */
	{"$RAR3$*1*0f263dd52eead558*834015cd*384*693*1*e28e9648f51b59e32f573b302f0e94aadf1050678b90c38dd4e750c7dd281d439ab4cccec5f1bd1ac40b6a1ead60c75625666307171e0fe2639d2397d5f68b97a2a1f733289eac0038b52ec6c3593ff07298fce09118c255b2747a02c2fa3175ab81166ebff2f1f104b9f6284a66f598764bd01f093562b5eeb9471d977bf3d33901acfd9643afe460e1d10b90e0e9bc8b77dc9ac40d40c2d211df9b0ecbcaea72c9d8f15859d59b3c85149b5bb5f56f0218cbbd9f28790777c39e3e499bc207289727afb2b2e02541b726e9ac028f4f05a4d7930efbff97d1ffd786c4a195bbed74997469802159f3b0ae05b703238da264087b6c2729d9023f67c42c5cbe40b6c67eebbfc4658dfb99bfcb523f62133113735e862c1430adf59c837305446e8e34fac00620b99f574fabeb2cd34dc72752014cbf4bd64d35f17cef6d40747c81b12d8c0cd4472089889a53f4d810b212fb314bf58c3dd36796de0feeefaf26be20c6a2fd00517152c58d0b1a95775ef6a1374c608f55f416b78b8c81761f1d*33:1::to-submit-challenges.txt", "wachtwoord"},
	{"$RAR3$*1*9759543e04fe3a22*834015cd*384*693*1*cdd2e2478e5153a581c47a201490f5d9b69e01584ae488a2a40203da9ba8c5271ed8edc8f91a7bd262bb5e5de07ecbe9e2003d054a314d16caf2ea1de9f54303abdee1ed044396f7e29c40c38e638f626442efd9f511b4743758cd4a6025c5af81d1252475964937d80bfd50d10c171e7e4041a66c02a74b2b451ae83b6807990fb0652a8cdab530c5a0c497575a6e6cbe2db2035217fe849d2e0b8693b70f3f97b757229b4e89c8273197602c23cc04ff5f24abf3d3c7eb686fc3eddce1bfe710cc0b6e8bd012928127da38c38dd8f056095982afacb4578f6280d51c6739739e033674a9413ca88053f8264c5137d4ac018125c041a3489daaf175ef75e9282d245b92948c1bbcf1c5f25b7028f6d207d87fe9598c2c7ccd1553e842a91ab8ca9261a51b14601a756070388d08039466dfa36f0b4c7ea7dd9ff25c9d98687203c58f9ec8757cafe4d2ed785d5a9e6d5ea838e4cc246a9e6d3c30979dcce56b380b05f9103e6443b35357550b50229c47f845a93a48602790096828d9d6bef0*33:1::to-submit-challenges.txt", "Sleepingbaby210"},
	{"$RAR3$*1*79e17c26407a7d52*834015cd*384*693*1*6844a189e732e9390b5a958b623589d5423fa432d756fd00940ac31e245214983507a035d4e0ee09469491551759a66c12150fe6c5d05f334fb0d8302a96d48ef4da04954222e0705507aaa84f8b137f284dbec344eee9cea6b2c4f63540c64df3ee8be3013466d238c5999e9a98eb6375ec5462869bba43401ec95077d0c593352339902c24a3324178e08fe694d11bfec646c652ffeafbdda929052c370ffd89168c83194fedf7c50fc7d9a1fbe64332063d267a181eb07b5d70a5854067db9b66c12703fde62728d3680cf3fdb9933a0f02bfc94f3a682ad5e7c428d7ed44d5ff554a8a445dea28b81e3a2631870e17f3f3c0c0204136802c0701590cc3e4c0ccd9f15e8be245ce9caa6969fab9e8443ac9ad9e73e7446811aee971808350c38c16c0d3372c7f44174666d770e3dd321e8b08fb2dc5e8a6a5b2a1720bad66e54abc194faabc5f24225dd8fee137ba5d4c2ed48c6462618e6333300a5b8dfc75c65608925e786eb0988f7b3a5ab106a55168d1001adc47ce95bba77b38c35b*33:1::to-submit-challenges.txt", "P-i-r-A-T-E"},
	{"$RAR3$*1*e1df79fd9ee1dadf*771a163b*64*39*1*edc483d67b94ab22a0a9b8375a461e06fa1108fa72970e16d962092c311970d26eb92a033a42f53027bdc0bb47231a12ed968c8d530a9486a90cbbc00040569b*33", "333"},
	{"$RAR3$*1*c83c00534d4af2db*771a163b*64*39*1*05244526d6b32cb9c524a15c79d19bba685f7fc3007a9171c65fc826481f2dce70be6148f2c3497f0d549aa4e864f73d4e4f697fdb66ff528ed1503d9712a414*33", "11eleven111"},
	{"$RAR3$*0*c203c4d80a8a09dc*49bbecccc08b5d893f308bce7ad36c0f", "sator"},
	{"$RAR3$*0*672fca155cb74ac3*8d534cd5f47a58f6493012cf76d2a68b", "arepo"},
	{"$RAR3$*0*c203c4d80a8a09dc*c3055efe7ca6587127fd541a5b88e0e4", "tenet"},
	{"$RAR3$*0*672fca155cb74ac3*c760267628f94060cca57be5896003c8", "opera"},
	{"$RAR3$*0*c203c4d80a8a09dc*1f406154556d4c895a8be207fd2b5d0c", "rotas"},
	{"$RAR3$*0*345f5f573a077ad7*638e388817cc7851e313406fd77730b9", "Boustrophedon"},
	{"$RAR3$*0*c9dea41b149b53b4*fcbdb66122d8ebdb32532c22ca7ab9ec", "password"},
	{"$RAR3$*0*7ce241baa2bd521b*f2b26d76424efa351c728b321671d074", "@"},
	{"$RAR3$*0*ea0ea55ce549c8ab*cf89099c620fcc244bdcbae55a616e76", "ow"},
	{"$RAR3$*0*ea0ea55ce549c8ab*6a35a76b1ce9ddc4229b9166d60dc113", "aes"},
	{"$RAR3$*0*ea0ea55ce549c8ab*1830771da109f53e2d6e626be16c2666", "sha1"},
	{"$RAR3$*0*7e52d3eba9bad316*ee8e1edd435cfa9b8ab861d958a4d588", "fiver"},
	{"$RAR3$*0*7e52d3eba9bad316*01987735ab0be7b6538470bd5f5fbf80", "magnum"},
	{"$RAR3$*0*7e52d3eba9bad316*f2fe986ed266c6617c48d04a429cf2e3", "7777777"},
	{"$RAR3$*0*7e52d3eba9bad316*f0ad6e7fdff9f82fff2aa990105fde21", "password"},
	{"$RAR3$*0*7ce241baa2bd521b*3eb0017fa8843017952c53a3ac8332b6", "nine9nine"},
	{"$RAR3$*0*7ce241baa2bd521b*ccbf0c3f8e059274606f33cc388b8a2f", "10tenten10"},
	{"$RAR3$*0*5fa43f823a60da63*af2630863e12046e42c4501c915636c9", "eleven11111"},
	{"$RAR3$*0*5fa43f823a60da63*88c0840d0bd98844173d35f867558ec2", "twelve121212"},
	{"$RAR3$*0*4768100a172fa2b6*48edcb5283ee2e4f0e8edb25d0d85eaa", "subconsciousness"},
#endif
	{NULL}
};

#if defined (_OPENMP)
static void lock_callback(int mode, int type, char *file, int line)
{
	(void)file;
	(void)line;
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&(lockarray[type]));
	else
		pthread_mutex_unlock(&(lockarray[type]));
}

static unsigned long thread_id(void)
{
	unsigned long ret;
	ret = (unsigned long) pthread_self();
	return (ret);
}

static void init_locks(void)
{
	int i;
	lockarray = (pthread_mutex_t*) OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
	for (i = 0; i < CRYPTO_num_locks(); i++)
		pthread_mutex_init(&(lockarray[i]), NULL);
	CRYPTO_set_id_callback((unsigned long (*)()) thread_id);
	CRYPTO_set_locking_callback((void (*)()) lock_callback);
}
#endif	/* _OPENMP */

/* Use AES-NI if available. This is not supported with low-level calls,
   we have to use EVP) */
static void init_aesni(void)
{
	ENGINE *e;
	const char *engine_id = "aesni";

	ENGINE_load_builtin_engines();
	e = ENGINE_by_id(engine_id);
	if (!e) {
		//fprintf(stderr, "AES-NI engine not available\n");
		return;
	}
	if (!ENGINE_init(e)) {
		fprintf(stderr, "AES-NI engine could not init\n");
		ENGINE_free(e);
		return;
	}
	if (!ENGINE_set_default(e, ENGINE_METHOD_ALL & ~ENGINE_METHOD_RAND)) {
		/* This should only happen when 'e' can't initialise, but the
		 * previous statement suggests it did. */
		fprintf(stderr, "AES-NI engine initialized but then failed\n");
		abort();
	}
	ENGINE_finish(e);
	ENGINE_free(e);
}

#ifndef __APPLE__ /* Apple segfaults on this :) */
static void openssl_cleanup(void)
{
	ENGINE_cleanup();
	ERR_free_strings();
	CRYPTO_cleanup_all_ex_data();
	EVP_cleanup();
}
#endif

#undef set_key
static void set_key(char *key, int index)
{
	int plen;
	UTF16 buf[PLAINTEXT_LENGTH + 1];

	/* UTF-16LE encode the password, encoding aware */
	plen = enc_to_utf16(buf, PLAINTEXT_LENGTH, (UTF8*) key, strlen(key));

	if (plen < 0)
		plen = strlen16(buf);

	memcpy(&saved_key[UNICODE_LENGTH * index], buf, UNICODE_LENGTH);

	saved_len[index] = plen << 1;
}

static void *get_salt(char *ciphertext)
{
	unsigned int i;
	size_t count;
	/* extract data from "salt" */
	char *encoded_salt;
	char *saltcopy = strdup(ciphertext);
	char *keep_ptr = saltcopy;
	static rarfile rarfile;

	saltcopy += 7;		/* skip over "$RAR3$*" */
	rarfile.type = atoi(strtok(saltcopy, "*"));
	encoded_salt = strtok(NULL, "*");
	for (i = 0; i < 8; i++)
		rarfile.salt[i] = atoi16[ARCH_INDEX(encoded_salt[i * 2])] * 16 + atoi16[ARCH_INDEX(encoded_salt[i * 2 + 1])];
	if (rarfile.type == 0) {	/* rar-hp mode */
		char *encoded_ct = strtok(NULL, "*");
		rarfile.raw_data = (unsigned char*)mem_alloc_tiny(16, MEM_ALIGN_WORD);
		for (i = 0; i < 16; i++)
			rarfile.raw_data[i] = atoi16[ARCH_INDEX(encoded_ct[i * 2])] * 16 + atoi16[ARCH_INDEX(encoded_ct[i * 2 + 1])];
	} else {
		char *p = strtok(NULL, "*");
		int inlined;
		for (i = 0; i < 4; i++)
			rarfile.crc.c[i] = atoi16[ARCH_INDEX(p[i * 2])] * 16 + atoi16[ARCH_INDEX(p[i * 2 + 1])];
		rarfile.pack_size = atoll(strtok(NULL, "*"));
		rarfile.unp_size = atoll(strtok(NULL, "*"));
		inlined = atoi(strtok(NULL, "*"));

		/* load ciphertext. We allocate and load all files here, and
		   they don't get unloaded until program ends */
		rarfile.raw_data = (unsigned char*)mem_alloc_tiny(rarfile.pack_size, MEM_ALIGN_WORD);
		if (inlined) {
			unsigned char *d = rarfile.raw_data;
			p = strtok(NULL, "*");
			for (i = 0; i < rarfile.pack_size; i++)
				*d++ = atoi16[ARCH_INDEX(p[i * 2])] * 16 + atoi16[ARCH_INDEX(p[i * 2 + 1])];
		} else {
			FILE *fp;
			rarfile.archive_name = strtok(NULL, "*");
			rarfile.pos = atol(strtok(NULL, "*"));

			if (!(fp = fopen(rarfile.archive_name, "rb"))) {
				fprintf(stderr, "! %s: %s\n", rarfile.archive_name, strerror(errno));
				error();
			}
			fseek(fp, rarfile.pos, SEEK_SET);
			count = fread(rarfile.raw_data, 1, rarfile.pack_size, fp);
			if (count != rarfile.pack_size) {
				fprintf(stderr, "Error loading file from archive '%s', expected %llu bytes, got %zu. Archive possibly damaged.\n", rarfile.archive_name, rarfile.pack_size, count);
				exit(0);
			}
			fclose(fp);
		}
		p = strtok(NULL, "*");
		rarfile.method = atoi16[ARCH_INDEX(p[0])] * 16 + atoi16[ARCH_INDEX(p[1])];
		if (rarfile.method != 0x30)
#if ARCH_LITTLE_ENDIAN
			rarfile.crc.w = ~rarfile.crc.w;
#else
			rarfile.crc.w = JOHNSWAP(~rarfile.crc.w);
#endif
	}
	MEM_FREE(keep_ptr);
	return (void*)&rarfile;
}

static void set_salt(void *salt)
{
	cur_file = (rarfile*)salt;
	memcpy(saved_salt, cur_file->salt, 8);
}

static void init(struct fmt_main *self)
{
#if defined (_OPENMP)
	omp_t = omp_get_max_threads();
	self->params.min_keys_per_crypt *= omp_t;
	self->params.max_keys_per_crypt = omp_t * OMP_SCALE * MAX_KEYS_PER_CRYPT;
	init_locks();
#endif /* _OPENMP */

	if (options.utf8)
		self->params.plaintext_length = MIN(125, 3 * PLAINTEXT_LENGTH);

	unpack_data = mem_calloc_tiny(sizeof(unpack_data_t) * omp_t, MEM_ALIGN_WORD);
	cracked = mem_calloc_tiny(sizeof(*cracked) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	saved_key = mem_calloc_tiny(UNICODE_LENGTH * self->params.max_keys_per_crypt, MEM_ALIGN_NONE);
	saved_len = mem_calloc_tiny(sizeof(*saved_len) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	saved_salt = mem_calloc_tiny(8, MEM_ALIGN_NONE);
	aes_key = mem_calloc_tiny(16 * self->params.max_keys_per_crypt, MEM_ALIGN_NONE);
	aes_iv = mem_calloc_tiny(16 * self->params.max_keys_per_crypt, MEM_ALIGN_NONE);

#ifdef DEBUG
	self->params.benchmark_comment = " (1-16 characters)";
#endif

	/* OpenSSL init */
	init_aesni();
	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();
#ifndef __APPLE__
	atexit(openssl_cleanup);
#endif
	/* CRC-32 table init, do it before we start multithreading */
	{
		CRC32_t crc;
		CRC32_Init(&crc);
	}
}

static int hexlen(char *q)
{
	char *s = q;
	size_t len = strlen(q);

	while (atoi16[ARCH_INDEX(*q)] != 0x7F)
		q++;
	return (len == (size_t)(q - s)) ? (int)(q - s) : -1 - (int)(q - s);
}

static int valid(char *ciphertext, struct fmt_main *self)
{
	char *ctcopy, *ptr, *keeptr;
	int mode;

	if (strncmp(ciphertext, "$RAR3$*", 7))
		return 0;
	if (!(ctcopy = strdup(ciphertext))) {
		fprintf(stderr, "Memory allocation failed in %s, unable to check if hash is valid!", FORMAT_LABEL);
		return 0;
	}
	keeptr = ctcopy;
	ctcopy += 7;
	if (!(ptr = strtok(ctcopy, "*"))) /* -p or -h mode */
		goto error;
	if (hexlen(ptr) != 1)
		goto error;
	mode = atoi(ptr);
	if (mode < 0 || mode > 1)
		goto error;
	if (!(ptr = strtok(NULL, "*"))) /* salt */
		goto error;
	if (hexlen(ptr) != 16) /* 8 bytes of salt */
		goto error;
	if (!(ptr = strtok(NULL, "*")))
		goto error;
	if (mode == 0) {
		if (hexlen(ptr) != 32) /* 16 bytes of encrypted known plain */
			goto error;
		MEM_FREE(keeptr);
		return 1;
	} else {
		int inlined;
		long long plen, ulen;

		if (hexlen(ptr) != 8) /* 4 bytes of CRC */
			goto error;
		if (!(ptr = strtok(NULL, "*"))) /* pack_size */
			goto error;
		if ((plen = atoll(ptr)) < 16)
			goto error;
		if (!(ptr = strtok(NULL, "*"))) /* unp_size */
			goto error;
		if ((ulen = atoll(ptr)) < 1)
			goto error;
		if (!(ptr = strtok(NULL, "*"))) /* inlined */
			goto error;
		if (hexlen(ptr) != 1)
			goto error;
		inlined = atoi(ptr);
		if (inlined < 0 || inlined > 1)
			goto error;
		if (!(ptr = strtok(NULL, "*"))) /* pack_size / archive_name */
			goto error;
		if (inlined) {
			if (hexlen(ptr) != plen * 2)
				goto error;
		} else {
			FILE *fp;
			char *archive_name;
			archive_name = ptr;
			if (!(fp = fopen(archive_name, "rb"))) {
				fprintf(stderr, "! %s: %s, skipping.\n", archive_name, strerror(errno));
				goto error;
			}
			if (!(ptr = strtok(NULL, "*"))) /* pos */
				goto error;
			/* We could go on and actually try seeking to pos
			   but this is enough for now */
			fclose(fp);
		}
		if (!(ptr = strtok(NULL, "*"))) /* method */
			goto error;
	}
	MEM_FREE(keeptr);
	return 1;

error:
#ifdef RAR_DEBUG
	{
		char buf[68];
		strnzcpy(buf, ciphertext, sizeof(buf));
		fprintf(stderr, "rejecting %s\n", buf);
	}
#endif
	MEM_FREE(keeptr);
	return 0;
}

static char *get_key(int index)
{
	UTF16 tmpbuf[PLAINTEXT_LENGTH + 1];

	memcpy(tmpbuf, &((UTF16*) saved_key)[index * PLAINTEXT_LENGTH], saved_len[index]);
	memset(&tmpbuf[saved_len[index] >> 1], 0, 2);
	return (char*) utf16_to_enc(tmpbuf);
}

#define ADD_BITS(n)	\
	{ \
		if (bits < 9) { \
			hold |= ((unsigned int)*next++ << (24 - bits)); \
			bits += 8; \
		} \
		hold <<= n; \
		bits -= n; \
	}

/*
 * This function is loosely based on JimF's check_inflate_CODE2() from
 * pkzip_fmt. Together with the other bit-checks, we are rejecting over 96%
 * of the candidates without resorting to a slow full check (which in turn
 * may reject semi-early, especially if it's a PPM block)
 *
 * Input is first 16 bytes of RAR buffer decrypted, as-is. It also contain the
 * first 2 bits, which have already been decoded, and have told us we had an
 * LZ block (RAR always use dynamic Huffman table) and keepOldTable was not set.
 *
 * RAR use 20 x (4 bits length, optionally 4 bits zerocount), and reversed
 * byte order.
 */
static MAYBE_INLINE int check_huffman(unsigned char *next) {
	unsigned int bits, hold, i;
	int left;
	unsigned int ncount[4];
	unsigned char *count = (unsigned char*)ncount;
	unsigned char bit_length[20];
	unsigned char *was = next;

#if ARCH_LITTLE_ENDIAN && ARCH_ALLOWS_UNALIGNED
	hold = JOHNSWAP(*(unsigned int*)next);
#else
	hold = next[3] + (((unsigned int)next[2]) << 8) +
		(((unsigned int)next[1]) << 16) +
		(((unsigned int)next[0]) << 24);
#endif
	next += 4;	// we already have the first 32 bits
	hold <<= 2;	// we already processed 2 bits, PPM and keepOldTable
	bits = 32 - 2;

	/* First, read 20 pairs of (bitlength[, zerocount]) */
	for (i = 0 ; i < 20 ; i++) {
		int length, zero_count;

		length = hold >> 28;
		ADD_BITS(4);
		if (length == 15) {
			zero_count = hold >> 28;
			ADD_BITS(4);
			if (zero_count == 0) {
				bit_length[i] = 15;
			} else {
				zero_count += 2;
				while (zero_count-- > 0 &&
				       i < sizeof(bit_length) /
				       sizeof(bit_length[0]))
					bit_length[i++] = 0;
				i--;
			}
		} else {
			bit_length[i] = length;
		}
	}

	if (next - was > 16) {
		fprintf(stderr, "*** (possible) BUG: check_huffman() needed %u bytes, we only have 16 (bits=%d, hold=0x%08x)\n", (int)(next - was), bits, hold);
		dump_stuff_msg("complete buffer", was, 16);
		error();
	}

	/* Count the number of codes for each code length */
	memset(count, 0, 16);
	for (i = 0; i < 20; i++) {
		++count[bit_length[i]];
	}

	count[0] = 0;
	if (!ncount[0] && !ncount[1] && !ncount[2] && !ncount[3])
		return 0; /* No codes at all */

	left = 1;
	for (i = 1; i < 16; ++i) {
		left <<= 1;
		left -= count[i];
		if (left < 0) {
			return 0; /* over-subscribed */
		}
	}
	if (left) {
		return 0; /* incomplete set */
	}
	return 1; /* Passed this check! */
}

static void crypt_all(int count)
{
	int index = 0;

#ifdef _OPENMP
#pragma omp parallel for
#endif
	for (index = 0; index < count; index++) {
		int i16 = index*16;
		unsigned int i, j;
#if ARCH_LITTLE_ENDIAN && ARCH_ALLOWS_UNALIGNED
		unsigned char RawPsw[UNICODE_LENGTH + 8 + sizeof(int)];
#else
		unsigned char RawPsw[UNICODE_LENGTH + 8];
#endif
		int RawLength;
		SHA_CTX ctx;
		unsigned int digest[5];
#if ARCH_LITTLE_ENDIAN && ARCH_ALLOWS_UNALIGNED
		unsigned int *PswNum;
#endif

#if ARCH_LITTLE_ENDIAN && ARCH_ALLOWS_UNALIGNED
		RawLength = saved_len[index] + 8 + 3;
		PswNum = (unsigned int*) &RawPsw[saved_len[index] + 8];
		*PswNum = 0;
#else
		RawLength = saved_len[index] + 8;
#endif
		/* derive IV and key for AES from saved_key and
		   saved_salt, this code block is based on unrarhp's
		   and unrar's sources */
		memcpy(RawPsw, &saved_key[UNICODE_LENGTH * index], saved_len[index]);
		memcpy(RawPsw + saved_len[index], saved_salt, 8);
		SHA1_Init(&ctx);
		for (i = 0; i < ROUNDS; i++) {
#if !(ARCH_LITTLE_ENDIAN && ARCH_ALLOWS_UNALIGNED)
			unsigned char PswNum[3];
#endif

			SHA1_Update(&ctx, RawPsw, RawLength);
#if ARCH_LITTLE_ENDIAN && ARCH_ALLOWS_UNALIGNED
			*PswNum += 1;
#else
			PswNum[0] = (unsigned char) i;
			PswNum[1] = (unsigned char) (i >> 8);
			PswNum[2] = (unsigned char) (i >> 16);
			SHA1_Update(&ctx, PswNum, 3);
#endif
			if (i % (ROUNDS / 16) == 0) {
				SHA_CTX tempctx = ctx;
				unsigned int tempout[5];

				SHA1_Final((unsigned char*) tempout, &tempctx);
#if ARCH_LITTLE_ENDIAN
				aes_iv[i16 + i / (ROUNDS / 16)] = (unsigned char)JOHNSWAP(tempout[4]);
#else
				aes_iv[i16 + i / (ROUNDS / 16)] = (unsigned char)tempout[4];
#endif
			}
		}
		SHA1_Final((unsigned char*)digest, &ctx);
#if ARCH_LITTLE_ENDIAN
		for (j = 0; j < 5; j++)	/* reverse byte order */
			digest[j] = JOHNSWAP(digest[j]);
#endif
		for (i = 0; i < 4; i++)
			for (j = 0; j < 4; j++)
				aes_key[i16 + i * 4 + j] = (unsigned char)(digest[i] >> (j * 8));
	}

#ifdef _OPENMP
#pragma omp parallel for
#endif
	for (index = 0; index < count; index++) {
		int i16 = index*16;
		unsigned int inlen = 16;
		int outlen;
		EVP_CIPHER_CTX aes_ctx;

		EVP_CIPHER_CTX_init(&aes_ctx);
		EVP_DecryptInit_ex(&aes_ctx, EVP_aes_128_cbc(), NULL, &aes_key[i16], &aes_iv[i16]);
		EVP_CIPHER_CTX_set_padding(&aes_ctx, 0);

		//fprintf(stderr, "key %s\n", utf16_to_enc((UTF16*)&saved_key[index * UNICODE_LENGTH]));
		/* AES decrypt, uses aes_iv, aes_key and raw_data */
		if (cur_file->type == 0) {	/* rar-hp mode */
			unsigned char plain[16];

			outlen = 0;

			EVP_DecryptUpdate(&aes_ctx, plain, &outlen, cur_file->raw_data, inlen);
			EVP_DecryptFinal_ex(&aes_ctx, &plain[outlen], &outlen);

			cracked[index] = !memcmp(plain, "\xc4\x3d\x7b\x00\x40\x07\x00", 7);

		} else {

			if (cur_file->method == 0x30) {	/* stored, not deflated */
				CRC32_t crc;
				unsigned char crc_out[4];
				unsigned char plain[0x8010];
				unsigned long long size = cur_file->unp_size;
				unsigned char *cipher = cur_file->raw_data;

				/* Use full decryption with CRC check.
				   Compute CRC of the decompressed plaintext */
				CRC32_Init(&crc);
				outlen = 0;

				while (size > 0x8000) {
					inlen = 0x8000;

					EVP_DecryptUpdate(&aes_ctx, plain, &outlen, cipher, inlen);
					CRC32_Update(&crc, plain, outlen > size ? size : outlen);
					size -= outlen;
					cipher += inlen;
				}
				EVP_DecryptUpdate(&aes_ctx, plain, &outlen, cipher, (size + 15) & ~0xf);
				EVP_DecryptFinal_ex(&aes_ctx, &plain[outlen], &outlen);
				size += outlen;
				CRC32_Update(&crc, plain, size);
				CRC32_Final(crc_out, crc);

				/* Compare computed CRC with stored CRC */
				cracked[index] = !memcmp(crc_out, &cur_file->crc.c, 4);
			} else {
				const int solid = 0;
				unpack_data_t *unpack_t;
				unsigned char plain[20];

				cracked[index] = 0;

				/* Decrypt just one block for early rejection */
				outlen = 0;
				EVP_DecryptUpdate(&aes_ctx, plain, &outlen, cur_file->raw_data, 16);
				EVP_DecryptFinal_ex(&aes_ctx, &plain[outlen], &outlen);

#if 1
				/* Early rejection */
				if (plain[0] & 0x80) {
					// PPM checks here.
					if (!(plain[0] & 0x20) ||  // Reset bit must be set
					    (plain[1] & 0x80))     // MaxMB must be < 128
						goto bailOut;
				} else {
					// LZ checks here.
					if ((plain[0] & 0x40) ||   // KeepOldTable can't be set
					    !check_huffman(plain)) // Huffman table check
						goto bailOut;
				}
#endif
				/* Reset stuff for full check */
				EVP_DecryptInit_ex(&aes_ctx, EVP_aes_128_cbc(), NULL, &aes_key[i16], &aes_iv[i16]);
				EVP_CIPHER_CTX_set_padding(&aes_ctx, 0);
#ifdef _OPENMP
				unpack_t = &unpack_data[omp_get_thread_num()];
#else
				unpack_t = unpack_data;
#endif
				unpack_t->max_size = cur_file->unp_size;
				unpack_t->dest_unp_size = cur_file->unp_size;
				unpack_t->pack_size = cur_file->pack_size;
				unpack_t->iv = &aes_iv[i16];
				unpack_t->ctx = &aes_ctx;
				unpack_t->key = &aes_key[i16];

				if (rar_unpack29(cur_file->raw_data, solid, unpack_t))
					cracked[index] = !memcmp(&unpack_t->unp_crc, &cur_file->crc.c, 4);
bailOut:;
			}
		}
		EVP_CIPHER_CTX_cleanup(&aes_ctx);
	}
}

static int cmp_all(void *binary, int count)
{
	int index;

	for (index = 0; index < count; index++)
		if (cracked[index])
			return 1;
	return 0;
}

static int cmp_one(void *binary, int index)
{
	return cracked[index];
}

static int cmp_exact(char *source, int index)
{
	return 1;
}

struct fmt_main fmt_rar = {
{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		SALT_SIZE,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_UNICODE | FMT_UTF8 | FMT_OMP,
		cpu_tests
	},{
		init,
		fmt_default_prepare,
		valid,
		fmt_default_split,
		fmt_default_binary,
		get_salt,
		{
			fmt_default_binary_hash
		},
		fmt_default_salt_hash,
		set_salt,
		set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
		{
			fmt_default_get_hash
		},
		cmp_all,
		cmp_one,
		cmp_exact
	}
};
