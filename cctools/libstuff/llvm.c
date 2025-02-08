#include <stdio.h>
#include <strings.h> /* cctools-port: For bcmp, bzero ... */
#include <stdlib.h>
#include <libc.h>
#include <sys/file.h>
#include <dlfcn.h>
#include <stdint.h> /* cctools-port */
#include <stdbool.h> /* cctools-port */
#include "llvm-c/Disassembler.h"
#include "stuff/llvm.h"
#include "stuff/allocate.h"
#include "stuff/xcode.h"
#include <mach-o/dyld.h>

/*
 * The disassembler API is currently exported from libLTO.dylib.  Eventually we
 * plan to include it (along with the current libLTO APIs) in a generic
 * libLLVM.dylib.
 */
#ifdef __APPLE__ /* cctools-port */
#define LIB_LLVM "libLTO.dylib"
#else
#define LIB_LLVM "libLTO.so"
#endif /* __APPLE__ */

static void *llvm_handle = NULL;
static void (*initialize)(void) = NULL;
static LLVMDisasmContextRef (*create)(const char *, void *, int,
	LLVMOpInfoCallback, LLVMSymbolLookupCallback) = NULL;
static LLVMDisasmContextRef (*createCPU)(const char *, const char *,void *, int,
	LLVMOpInfoCallback, LLVMSymbolLookupCallback) = NULL;
static void (*dispose)(LLVMDisasmContextRef) = NULL;
static size_t (*disasm)(LLVMDisasmContextRef, uint8_t *, uint64_t, uint64_t,
	char *, size_t) = NULL;
static int (*options)(LLVMDisasmContextRef, uint64_t) = NULL;
static const char * (*version)(void) = NULL;

/*
 * load_llvm_disasm() will try to dynamically load libLTO.dylib once, and on
 * success set llvm_handle to the value returned by dlopen() and set the disasm
 * function pointers.
 */
static void load_llvm_disasm(void)
{
	static int tried_to_load_llvm;

	if(tried_to_load_llvm == 0){
	    tried_to_load_llvm = 1;

	    llvm_handle = llvm_load();
	    if(llvm_handle == NULL)
		return;

	    create = dlsym(llvm_handle, "LLVMCreateDisasm");
	    dispose = dlsym(llvm_handle, "LLVMDisasmDispose");
	    disasm = dlsym(llvm_handle, "LLVMDisasmInstruction");

	    /* Note we allow these to not be defined */
	    options = dlsym(llvm_handle, "LLVMSetDisasmOptions");
	    createCPU = dlsym(llvm_handle, "LLVMCreateDisasmCPU");
	    version = dlsym(llvm_handle, "lto_get_version");

#ifndef __APPLE__ /* cctools-port */
	    if(create == NULL){
		fprintf(stderr, "Your " LIB_LLVM " lacks required symbols.\n");
		fprintf(stderr, "Please run: tools/fix_liblto.sh (or http://git.io/AyZP) to fix libLTO.\n");
	    }
#endif /* __APPLE__ */

	    if(create == NULL ||
	       dispose == NULL ||
	       disasm == NULL){

		dlclose(llvm_handle);
		llvm_handle = NULL;
		create = NULL;
		createCPU = NULL;
		dispose = NULL;
		disasm = NULL;
		options = NULL;
		version = NULL;
		return;
	    }
	}
}

void* llvm_load(void)
{
    static int tried_to_load_llvm;

    if (!tried_to_load_llvm) {
	tried_to_load_llvm = 1;

	/*
	 * First try to load libLTO.dylib from an environment override. This is
	 * the full path to the libLTO.dylib. We're keeping this override
	 * separate from DYLD_LIBRARY_PATH just to be explicit, and make it
	 * easier to work with tools like Xcode.
	 */
	if (llvm_handle == NULL) {
	    const char* lto_path = getenv("LIBLTO_PATH");
	    if (lto_path) {
		llvm_handle = dlopen(lto_path, RTLD_NOW);
	    }
	}

	/*
	 * Next, try to load libLTO.dylib from a location relative to the
	 * currently running tool. In a sensible install, this the version of
	 * libLTO.dylib that this tool is most compatible with.
	 */
	if (llvm_handle == NULL) {
	    uint32_t bufsize;
	    char *p, *prefix, *llvm_path, *exec_path;
	    char buf[MAXPATHLEN], resolved_name[PATH_MAX];
	    int i;

	    /* get the executable path. */
	    bufsize = MAXPATHLEN;
	    exec_path = buf;
	    i = _NSGetExecutablePath(exec_path, &bufsize);
	    if(i == -1){
		exec_path = allocate(bufsize);
		_NSGetExecutablePath(exec_path, &bufsize);
	    }

	    /* now get the real executable path. */
	    prefix = realpath(exec_path, resolved_name);
	    if (exec_path != buf)
		free(exec_path);

	    /*
	     * create a new path where the executable name is replaced with
	     * a relative path to the library location.
	     */
	    /* cctools-port: added  prefix ? */
	    p = (prefix ? rindex(prefix, '/') : NULL);
	    if(p != NULL)
		p[1] = '\0';
	    llvm_path = makestr(prefix, "../lib/" LIB_LLVM, NULL);

	    /* LOAD! */
	    llvm_handle = dlopen(llvm_path, RTLD_NOW);
	    free(llvm_path);
	}

#ifdef __APPLE__ /* cctools-port */
	/* The expected library is missing; fall back to the current Xcode. */
	if(llvm_handle == NULL){
	    const char* xcode = xcode_developer_path();
	    if (xcode) {
		char* llvm_path;

		llvm_path = makestr(xcode,
				    "/Toolchains/XcodeDefault.xctoolchain"
				    "/usr/lib/" LIB_LLVM, NULL);
		llvm_handle = dlopen(llvm_path, RTLD_NOW);
		free(llvm_path);
	    }
	}

	/* Finally, just try a hardcoded fallback. */
	if(llvm_handle == NULL){
	    llvm_handle = dlopen("/Applications/Xcode.app/Contents/"
				 "Developer/Toolchains/XcodeDefault."
				 "xctoolchain/usr/lib/" LIB_LLVM,
				 RTLD_NOW);
	}
#endif /* __APPLE__ */
	}

    return llvm_handle;
}

/*
 * Wrapper to dynamically load LIB_LLVM and call LLVMCreateDisasm().
 */
__private_extern__
LLVMDisasmContextRef
llvm_create_disasm(
const char *TripleName,
const char *CPU,
void *DisInfo,
int TagType,
LLVMOpInfoCallback GetOpInfo,
LLVMSymbolLookupCallback SymbolLookUp)
{
   LLVMDisasmContextRef DC;

	load_llvm_disasm();
	if(llvm_handle == NULL)
	    return(NULL);

	/*
	 * Note this was added after the interface was defined, so it may
	 * be undefined.  But if not we must call it first.
	 */
	initialize = dlsym(llvm_handle, "lto_initialize_disassembler");
	if(initialize != NULL)
	    initialize();

	if(*CPU != '\0' && createCPU != NULL)
	    DC = createCPU(TripleName, CPU, DisInfo, TagType, GetOpInfo,
			   SymbolLookUp);
	else
	    DC = create(TripleName, DisInfo, TagType, GetOpInfo, SymbolLookUp);
	return(DC);
}

/*
 * Wrapper to call LLVMDisasmDispose().
 */
__private_extern__
void
llvm_disasm_dispose(
LLVMDisasmContextRef DC)
{
	if(dispose != NULL)
	    dispose(DC);
}

/*
 * Wrapper to call LLVMDisasmInstruction().
 */
__private_extern__
size_t
llvm_disasm_instruction(
LLVMDisasmContextRef DC,
uint8_t *Bytes,
uint64_t BytesSize,
uint64_t Pc,
char *OutString,
size_t OutStringSize)
{

	if(disasm == NULL)
	    return(0);
	return(disasm(DC, Bytes, BytesSize, Pc, OutString, OutStringSize));
}

/*
 * Wrapper to call LLVMSetDisasmOptions().
 */
__private_extern__
int
llvm_disasm_set_options(
LLVMDisasmContextRef DC,
uint64_t Options)
{

	if(options == NULL)
	    return(0);
	return(options(DC, Options));
}

/*
 * Wrapper to call lto_get_version().
 */
__private_extern__
const char *
llvm_disasm_version_string(void)
{
	load_llvm_disasm();
	if(llvm_handle == NULL)
	    return(NULL);
	if(version == NULL)
	    return(NULL);
	return(version());
}

/*
 * GetOpInfo added an extra parameter after clang-1403. Parse the version string
 * to see if we should provide the new or old callback.
 */
__private_extern__
int
llvm_disasm_new_getopinfo_abi(void)
{
	const char *VersionStr = llvm_disasm_version_string();

	// LTO is not an Apple-built one, most likely a developer's own just
	// compiled and so new.
	if (!VersionStr)
	    return true;

	const char *VersionPos = strstr(VersionStr, "clang-");

	if (!VersionPos)
	    return true;

	VersionPos += strlen("clang-");
	char *EndPtr;
	long Version = strtol(VersionPos, &EndPtr, 10);

	if (EndPtr == VersionPos)
	    return true;

	return Version > 1403;
}
