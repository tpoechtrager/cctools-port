/* public domain sha256 implementation based on fips180-3 */
#ifndef __SHA256_H
#define __SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
	uint64_t len;    /* processed message length */
	uint32_t h[8];   /* hash state */
	uint8_t buf[64]; /* message block buffer */
} SHA256_CTX;

#define SHA256_DIGEST_SIZE 32

/* reset state */
void SHA256_Init(SHA256_CTX *ctx);
/* process message */
void SHA256_Update(SHA256_CTX *ctx, const uint8_t *m, size_t len);
/* get message digest */
/* state is ruined after sum, keep a copy if multiple sum is needed */
/* part of the message might be left in s, zero it if secrecy is needed */
void SHA256_Final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif                          /* __SHA256_H */
