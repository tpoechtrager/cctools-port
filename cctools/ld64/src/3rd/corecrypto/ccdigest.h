#ifdef __APPLE__

#include_next <corecrypto/ccdigest.h>

#else
#ifndef __CORECRYPTO_CCDIGEST__
#define __CORECRYPTO_CCDIGEST__

#include <stdint.h>
#include <stddef.h>

struct ccdigest_ctx {
	uint8_t dummy;
};

struct ccdigest_info {
	uint64_t state_len;
	void (*init)(struct ccdigest_ctx *ctx);
	void (*update)(struct ccdigest_ctx *ctx, size_t size, const void *data);
	void (*final)(struct ccdigest_ctx *ctx, void *digest);
};

#define ccdigest_di_decl(di, ctx) struct ccdigest_ctx *ctx = calloc(1, di->state_len)
#define ccdigest_init(di, ...) (di)->init(__VA_ARGS__)
#define ccdigest_update(di, ...) (di)->update(__VA_ARGS__)
#define ccdigest_final(di, ...) (di)->final(__VA_ARGS__)

#endif /* __CORECRYPTO_CCDIGEST__ */
#endif /* __APPLE__ */
