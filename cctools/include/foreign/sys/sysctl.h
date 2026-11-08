/* Linux uses sysconf and explicit cross-target defaults instead of sysctl. */
#ifndef __linux__
#include_next <sys/sysctl.h>
#endif
