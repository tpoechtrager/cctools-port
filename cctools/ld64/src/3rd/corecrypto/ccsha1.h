#ifdef __APPLE__

#include_next <corecrypto/ccsha1.h>

#else
#ifndef __CORECRYPTO_CCSHA1__
#define __CORECRYPTO_CCSHA1__

#ifdef __cplusplus
extern "C"
{
#endif

const struct ccdigest_info *ccsha1_di(void);

#ifdef __cplusplus
}
#endif

#endif /* __CORECRYPTO_CCSHA1__ */
#endif /* __APPLE__ */
