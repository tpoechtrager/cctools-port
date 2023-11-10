// compat_corecrypto.h
#ifndef COMPAT_CORECRYPTO_H
#define COMPAT_CORECRYPTO_H

#include "sha256.h"
#include "sha1.h"

#define CCSHA256_OUTPUT_SIZE SHA256_DIGEST_SIZE
#define CCSHA1_OUTPUT_SIZE   SHA1_DIGEST_SIZE

typedef SHA256_CTX ccsha256_ctx;
typedef SHA1_CTX   ccsha1_ctx;

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
    ccsha256_ctx sha256;
    ccsha1_ctx sha1;
} cc_digest_ctx;

typedef struct ccdigest_info {
    unsigned int output_size;
    void (*init)(void*);
    void (*update)(void*, const void*, size_t);
    void (*final)(void*, unsigned char*);
} ccdigest_info;

static inline const struct ccdigest_info* ccsha1_di(void) {
    static const struct ccdigest_info di_sha1 = {
        CCSHA1_OUTPUT_SIZE,
        (void (*)(void*))SHA1_Init,
        (void (*)(void*, const void*, size_t))SHA1_Update,
        (void (*)(void*, unsigned char*))SHA1_Final
    };
    return &di_sha1;
}

static inline const struct ccdigest_info* ccsha256_di(void) {
    static const struct ccdigest_info di_sha256 = {
        CCSHA256_OUTPUT_SIZE,
        (void (*)(void*))SHA256_Init,
        (void (*)(void*, const void*, size_t))SHA256_Update,
        (void (*)(void*, unsigned char*))SHA256_Final
    };
    return &di_sha256;
}

#ifdef __cplusplus
}
#endif

// Macro to declare a digest context variable
#define ccdigest_di_decl(di, ctx) cc_digest_ctx ctx

#define ccdigest_init(di, ctx) ((di)->output_size == CCSHA256_OUTPUT_SIZE ? \
    (di)->init(&(ctx).sha256) : (di)->init(&(ctx).sha1))

#define ccdigest_update(di, ctx, len, data) ((di)->output_size == CCSHA256_OUTPUT_SIZE ? \
    (di)->update(&(ctx).sha256, (const uint8_t*)(data), len) : (di)->update(&(ctx).sha1, (const uint8_t*)(data), len))

#define ccdigest_final(di, ctx, digest) ((di)->output_size == CCSHA256_OUTPUT_SIZE ? \
    (di)->final(&(ctx).sha256, digest) : (di)->final(&(ctx).sha1, digest))

#define kCCDigestSHA256 1
#define kCCDigestSHA1   2

static inline int CCDigest(unsigned int algorithm, const void *data, size_t length, unsigned char *output) {
    cc_digest_ctx ctx;
    if (algorithm == kCCDigestSHA256) {
        SHA256_Init(&ctx.sha256);
        SHA256_Update(&ctx.sha256, (const uint8_t*)data, length);
        SHA256_Final(&ctx.sha256, output);
        return 0;
    } else if (algorithm == kCCDigestSHA1) {
        SHA1_Init(&ctx.sha1);
        SHA1_Update(&ctx.sha1, (const uint8_t*)data, length);
        SHA1_Final(&ctx.sha1, output);
        return 0;
    } else {
        return -1;
    }
}

#endif // COMPAT_CORECRYPTO_H
