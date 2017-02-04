#ifndef _HELPER_H
#define _HELPER_H

#ifndef __USE_GNU
#define __USE_GNU
#endif

#ifndef __has_extension
#define __has_extension(x) 0
#endif

#ifndef __has_include
#define __has_include(x) 0
#endif

#ifndef __has_include_next
#define __has_include_next(x) 0
#endif

#ifdef __NetBSD__
#define stoi(a,b) atoi(a.c_str()); do { if (!b) break; const char *p = a.c_str(); *b = 0; while (isdigit(*p++)) (*b)++; } while (0)
#endif

/* gcc 4.7 does not have std::map::emplace */
#define STD_MAP_EMPLACE(KEY_TYPE, VALUE_TYPE, set, _first, _second) \
({ \
    std::pair<decltype(set)::iterator, bool> ret; \
    bool f = false; \
    for (decltype(set)::iterator it = set.begin(); it != set.end(); ++it) if (it->first == _first) \
    { \
        ret = std::pair<decltype(set)::iterator, bool>(it, false); \
        f = true; \
        break; \
    } \
    if (!f) ret = set.insert(std::pair<KEY_TYPE, VALUE_TYPE>(_first, _second)); \
    ret; \
})

#ifdef __cplusplus
extern "C" {
#endif

#include <mach/mach_time.h>
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <sys/time.h>
#include <dlfcn.h>

struct dyld_unwind_sections
{
    const struct mach_header*      mh;
    const void*                    dwarf_section;
    intptr_t                       dwarf_section_length;
    const void*                    compact_unwind_section;
    intptr_t                       compact_unwind_section_length;
};

typedef Dl_info dl_info;

#ifndef __APPLE__
typedef char uuid_string_t__[37];
#define uuid_string_t uuid_string_t__
#endif

int _NSGetExecutablePath(char *path, unsigned int *size);
int _dyld_find_unwind_sections(void* i, struct dyld_unwind_sections* sec);
mach_port_t mach_host_self(void);
kern_return_t host_statistics ( host_t host_priv, host_flavor_t flavor, host_info_t host_info_out, mach_msg_type_number_t *host_info_outCnt);
uint64_t mach_absolute_time(void);

#ifdef __cplusplus
}
#endif

#endif
