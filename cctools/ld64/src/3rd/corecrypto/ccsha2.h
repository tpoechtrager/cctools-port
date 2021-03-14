#ifdef __APPLE__

#include_next <corecrypto/ccsha1.h>

#else
#ifndef __CORECRYPTO_CCSHA256__
#define __CORECRYPTO_CCSHA256__

#ifdef __cplusplus
extern "C"
{
#endif

const struct ccdigest_info *ccsha256_di(void);

#ifdef __cplusplus
}
#endif

#endif /* __CORECRYPTO_CCSHA256__ */
#endif /* __APPLE__ */
