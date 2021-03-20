#ifndef __APPLE__

#include <corecrypto/ccdigest.h>
#include <corecrypto/ccsha1.h>

#include "sha1.h"

static void ccsha1_init(struct ccdigest_ctx *ctx_) {
	SHA1_CTX *ctx = (SHA1_CTX *)ctx_;
	SHA1_Init(ctx);
}

static void ccsha1_update(struct ccdigest_ctx *ctx_, size_t size, const void *data) {
	SHA1_CTX *ctx = (SHA1_CTX *)ctx_;
	SHA1_Update(ctx, (const uint8_t *)data, size);
}

static void ccsha1_final(struct ccdigest_ctx *ctx_, void *digest) {
	SHA1_CTX *ctx = (SHA1_CTX *)ctx_;
	SHA1_Final(ctx, digest);
}

static const struct ccdigest_info ccsha1_info = {
	sizeof(SHA1_CTX),
	ccsha1_init,
	ccsha1_update,
	ccsha1_final
};

const struct ccdigest_info *ccsha1_di(void) {
	return &ccsha1_info;
}

#endif /* __APPLE__ */
