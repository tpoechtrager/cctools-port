#include_next <unistd.h>

#ifndef L_SET
#define L_SET SEEK_SET /* Cygwin */
#endif
