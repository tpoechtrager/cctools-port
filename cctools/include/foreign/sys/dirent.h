#include_next <sys/dirent.h>

#ifndef MAXNAMLEN
#define MAXNAMLEN NAME_MAX /* DragonFly BSD */
#endif
