/*
  Copyright (c) 2015 Thomas Poechtrager (t.poechtrager@gmail.com)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
 */

#include "llvm-c/lto.h"

#ifndef __APPLE__

//
// This is workaround is required for
// ld64 `-lto_library' support on non-Apple OSs.
//

#include <dlfcn.h>

#if LTO_API_VERSION > 17
#warning header file out of date
#endif

#define LIBLTO "libLTO.so"

#define LTO_STR1(x) #x
#define LTO_STR(x) LTO_STR1(x)

#define LTO_DECLARE(symbol) \
  __typeof(&symbol) ltoproxy_##symbol

LTO_DECLARE(lto_get_version);
LTO_DECLARE(lto_get_error_message);
LTO_DECLARE(lto_module_is_object_file);
LTO_DECLARE(lto_module_is_object_file_for_target);
LTO_DECLARE(lto_module_is_object_file_in_memory);
LTO_DECLARE(lto_module_is_object_file_in_memory_for_target);
LTO_DECLARE(lto_module_create);
LTO_DECLARE(lto_module_create_from_memory);
#if LTO_API_VERSION >= 9
LTO_DECLARE(lto_module_create_from_memory_with_path);
#endif // LTO_API_VERSION >= 9
#if LTO_API_VERSION >= 11
LTO_DECLARE(lto_module_create_in_local_context);
LTO_DECLARE(lto_module_create_in_codegen_context);
#endif // LTO_API_VERSION >= 11
#if LTO_API_VERSION >= 5
LTO_DECLARE(lto_module_create_from_fd);
LTO_DECLARE(lto_module_create_from_fd_at_offset);
#endif // LTO_API_VERSION >= 5
LTO_DECLARE(lto_module_dispose);
#if LTO_API_VERSION >= 4
LTO_DECLARE(lto_module_get_target_triple);
LTO_DECLARE(lto_module_set_target_triple);
#endif // LTO_API_VERSION >= 4
LTO_DECLARE(lto_module_get_num_symbols);
LTO_DECLARE(lto_module_get_symbol_name);
LTO_DECLARE(lto_module_get_symbol_attribute);
#if LTO_API_VERSION >= 16
LTO_DECLARE(lto_module_get_linkeropts);
#endif // LTO_API_VERSION >= 17
#if LTO_API_VERSION >= 7
LTO_DECLARE(lto_codegen_set_diagnostic_handler);
#endif // LTO_API_VERSION >= 7
LTO_DECLARE(lto_codegen_create);
#if LTO_API_VERSION >= 11
LTO_DECLARE(lto_codegen_create_in_local_context);
#endif // LTO_API_VERSION >= 11
LTO_DECLARE(lto_codegen_dispose);
LTO_DECLARE(lto_codegen_add_module);
#if LTO_API_VERSION >= 13
LTO_DECLARE(lto_codegen_set_module);
#endif // LTO_API_VERSION >= 13
LTO_DECLARE(lto_codegen_set_debug_model);
LTO_DECLARE(lto_codegen_set_pic_model);
#if LTO_API_VERSION >= 4
LTO_DECLARE(lto_codegen_set_cpu);
#endif // LTO_API_VERSION >= 4
LTO_DECLARE(lto_codegen_set_assembler_path);
#if LTO_API_VERSION >= 4
LTO_DECLARE(lto_codegen_set_assembler_args);
#endif
LTO_DECLARE(lto_codegen_add_must_preserve_symbol);
#if LTO_API_VERSION >= 4
LTO_DECLARE(lto_codegen_write_merged_modules);
#endif // LTO_API_VERSION >= 4
LTO_DECLARE(lto_codegen_compile);
#if LTO_API_VERSION >= 5
LTO_DECLARE(lto_codegen_compile_to_file);
#endif // LTO_API_VERSION >= 5
#if LTO_API_VERSION >= 12
LTO_DECLARE(lto_codegen_optimize);
LTO_DECLARE(lto_codegen_compile_optimized);
LTO_DECLARE(lto_api_version);
#endif // LTO_API_VERSION >= 12
LTO_DECLARE(lto_codegen_debug_options);
#if LTO_API_VERSION >= 5
LTO_DECLARE(lto_initialize_disassembler);
#endif // LTO_API_VERSION >= 5
#if LTO_API_VERSION >= 14
LTO_DECLARE(lto_codegen_set_should_internalize);
#endif // LTO_API_VERSION >= 14
#if LTO_API_VERSION >= 15
LTO_DECLARE(lto_codegen_set_should_embed_uselists);
#endif // LTO_API_VERSION >= 14

#ifndef __CYGWIN__
__attribute__((weak))
#endif
extern const char* sOverridePathlibLTO;

namespace ltoproxy {

static const char *getLibLTOPath() {
  return &sOverridePathlibLTO && sOverridePathlibLTO ?
         sOverridePathlibLTO : LIBLTO;
}

}

#define LTO_LOOKUP(symbol)                                                             \
  ltoproxy_##symbol =                                                                  \
    __extension__ reinterpret_cast<__typeof(&symbol)>(dlsym(handle, LTO_STR(symbol))); \
  if ( !ltoproxy_##symbol )                                                            \
    throwf("required symbol `" LTO_STR(symbol) "' not found in %s;", getLibLTOPath());

namespace ltoproxy {

static void* handle = NULL;

static void deinitLibLTO() {
  dlclose(handle);
  handle = NULL;
}

static void initLibLTO() {
    handle = dlopen(getLibLTOPath(), RTLD_NOW);

    if ( !handle )
        throwf("couldn't dlopen() %s: %s;", getLibLTOPath(), dlerror());

    atexit(deinitLibLTO);

    LTO_LOOKUP(lto_get_version);
    LTO_LOOKUP(lto_get_error_message);
    LTO_LOOKUP(lto_module_is_object_file);
    LTO_LOOKUP(lto_module_is_object_file_for_target);
    LTO_LOOKUP(lto_module_is_object_file_in_memory);
    LTO_LOOKUP(lto_module_is_object_file_in_memory_for_target);
    LTO_LOOKUP(lto_module_create);
    LTO_LOOKUP(lto_module_create_from_memory);
#if LTO_API_VERSION >= 9
    LTO_LOOKUP(lto_module_create_from_memory_with_path);
#endif // LTO_API_VERSION >= 9
#if LTO_API_VERSION >= 11
    LTO_LOOKUP(lto_module_create_in_local_context);
    LTO_LOOKUP(lto_module_create_in_codegen_context);
#endif // LTO_API_VERSION >= 11
#if LTO_API_VERSION >= 5
    LTO_LOOKUP(lto_module_create_from_fd);
    LTO_LOOKUP(lto_module_create_from_fd_at_offset);
#endif // LTO_API_VERSION >= 5
    LTO_LOOKUP(lto_module_dispose);
#if LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_module_get_target_triple);
    LTO_LOOKUP(lto_module_set_target_triple);
#endif // LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_module_get_num_symbols);
    LTO_LOOKUP(lto_module_get_symbol_name);
    LTO_LOOKUP(lto_module_get_symbol_attribute);
#if LTO_API_VERSION >= 16
    LTO_LOOKUP(lto_module_get_linkeropts);
#endif // LTO_API_VERSION >= 17
#if LTO_API_VERSION >= 7
    LTO_LOOKUP(lto_codegen_set_diagnostic_handler);
#endif // LTO_API_VERSION >= 7
    LTO_LOOKUP(lto_codegen_create);
#if LTO_API_VERSION >= 11
    LTO_LOOKUP(lto_codegen_create_in_local_context);
#endif // LTO_API_VERSION >= 11
    LTO_LOOKUP(lto_codegen_dispose);
    LTO_LOOKUP(lto_codegen_add_module);
#if LTO_API_VERSION >= 13
    LTO_LOOKUP(lto_codegen_set_module);
#endif // LTO_API_VERSION >= 13
    LTO_LOOKUP(lto_codegen_set_debug_model);
    LTO_LOOKUP(lto_codegen_set_pic_model);
#if LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_codegen_set_cpu);
#endif // LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_codegen_set_assembler_path);
#if LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_codegen_set_assembler_args);
#endif
    LTO_LOOKUP(lto_codegen_add_must_preserve_symbol);
#if LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_codegen_write_merged_modules);
#endif // LTO_API_VERSION >= 4
    LTO_LOOKUP(lto_codegen_compile);
#if LTO_API_VERSION >= 5
    LTO_LOOKUP(lto_codegen_compile_to_file);
#endif // LTO_API_VERSION >= 5
#if LTO_API_VERSION >= 12
    LTO_LOOKUP(lto_codegen_optimize);
    LTO_LOOKUP(lto_codegen_compile_optimized);
    LTO_LOOKUP(lto_api_version);
#endif // LTO_API_VERSION >= 12
    LTO_LOOKUP(lto_codegen_debug_options);
#if LTO_API_VERSION >= 5
    LTO_LOOKUP(lto_initialize_disassembler);
#endif // LTO_API_VERSION >= 5
#if LTO_API_VERSION >= 14
    LTO_LOOKUP(lto_codegen_set_should_internalize);
#endif // LTO_API_VERSION >= 14
#if LTO_API_VERSION >= 15
    LTO_LOOKUP(lto_codegen_set_should_embed_uselists);
#endif // LTO_API_VERSION >= 14
}

}

#define LTO_WRAPPER_FUN(fn, ...)       \
([&]() {                               \
    if ( !ltoproxy::handle )           \
        ltoproxy::initLibLTO();        \
    return ltoproxy_##fn(__VA_ARGS__); \
}())

#define lto_get_version(...)                                LTO_WRAPPER_FUN(lto_get_version, __VA_ARGS__)
#define lto_get_error_message(...)                          LTO_WRAPPER_FUN(lto_get_error_message, __VA_ARGS__)
#define lto_module_is_object_file(...)                      LTO_WRAPPER_FUN(lto_module_is_object_file, __VA_ARGS__)
#define lto_module_is_object_file_for_target(...)           LTO_WRAPPER_FUN(lto_module_is_object_file_for_target, __VA_ARGS__)
#define lto_module_is_object_file_in_memory(...)            LTO_WRAPPER_FUN(lto_module_is_object_file_in_memory, __VA_ARGS__)
#define lto_module_is_object_file_in_memory_for_target(...) LTO_WRAPPER_FUN(lto_module_is_object_file_in_memory_for_target, __VA_ARGS__)
#define lto_module_create(...)                              LTO_WRAPPER_FUN(lto_module_create, __VA_ARGS__)
#define lto_module_create_from_memory(...)                  LTO_WRAPPER_FUN(lto_module_create_from_memory, __VA_ARGS__)
#define lto_module_create_from_memory_with_path(...)        LTO_WRAPPER_FUN(lto_module_create_from_memory_with_path, __VA_ARGS__)
#define lto_module_create_in_local_context(...)             LTO_WRAPPER_FUN(lto_module_create_in_local_context, __VA_ARGS__)
#define lto_module_create_in_codegen_context(...)           LTO_WRAPPER_FUN(lto_module_create_in_codegen_context, __VA_ARGS__)
#define lto_module_create_from_fd(...)                      LTO_WRAPPER_FUN(lto_module_create_from_fd, __VA_ARGS__)
#define lto_module_create_from_fd_at_offset(...)            LTO_WRAPPER_FUN(lto_module_create_from_fd_at_offset, __VA_ARGS__)
#define lto_module_dispose(...)                             LTO_WRAPPER_FUN(lto_module_dispose, __VA_ARGS__)
#define lto_module_get_target_triple(...)                   LTO_WRAPPER_FUN(lto_module_get_target_triple, __VA_ARGS__)
#define lto_module_set_target_triple(...)                   LTO_WRAPPER_FUN(lto_module_set_target_triple, __VA_ARGS__)
#define lto_module_get_num_symbols(...)                     LTO_WRAPPER_FUN(lto_module_get_num_symbols, __VA_ARGS__)
#define lto_module_get_symbol_name(...)                     LTO_WRAPPER_FUN(lto_module_get_symbol_name, __VA_ARGS__)
#define lto_module_get_symbol_attribute(...)                LTO_WRAPPER_FUN(lto_module_get_symbol_attribute, __VA_ARGS__)
#define lto_module_get_linkeropts(...)                      LTO_WRAPPER_FUN(lto_module_get_linkeropts, __VA_ARGS__)
#define lto_codegen_set_diagnostic_handler(...)             LTO_WRAPPER_FUN(lto_codegen_set_diagnostic_handler, __VA_ARGS__)
#define lto_codegen_create(...)                             LTO_WRAPPER_FUN(lto_codegen_create, __VA_ARGS__)
#define lto_codegen_dispose(...)                            LTO_WRAPPER_FUN(lto_codegen_dispose, __VA_ARGS__)
#define lto_codegen_add_module(...)                         LTO_WRAPPER_FUN(lto_codegen_add_module, __VA_ARGS__)
#define lto_codegen_set_module(...)                         LTO_WRAPPER_FUN(lto_codegen_set_module, __VA_ARGS__)
#define lto_codegen_set_debug_model(...)                    LTO_WRAPPER_FUN(lto_codegen_set_debug_model, __VA_ARGS__)
#define lto_codegen_set_pic_model(...)                      LTO_WRAPPER_FUN(lto_codegen_set_pic_model, __VA_ARGS__)
#define lto_codegen_set_cpu(...)                            LTO_WRAPPER_FUN(lto_codegen_set_cpu, __VA_ARGS__)
#define lto_codegen_set_assembler_path(...)                 LTO_WRAPPER_FUN(lto_codegen_set_assembler_path, __VA_ARGS__)
#define lto_codegen_set_assembler_args(...)                 LTO_WRAPPER_FUN(lto_codegen_set_assembler_args, __VA_ARGS__)
#define lto_codegen_add_must_preserve_symbol(...)           LTO_WRAPPER_FUN(lto_codegen_add_must_preserve_symbol, __VA_ARGS__)
#define lto_codegen_write_merged_modules(...)               LTO_WRAPPER_FUN(lto_codegen_write_merged_modules, __VA_ARGS__)
#define lto_codegen_compile(...)                            LTO_WRAPPER_FUN(lto_codegen_compile, __VA_ARGS__)
#define lto_codegen_compile_to_file(...)                    LTO_WRAPPER_FUN(lto_codegen_compile_to_file, __VA_ARGS__)
#define lto_codegen_optimize(...)                           LTO_WRAPPER_FUN(lto_codegen_optimize, __VA_ARGS__)
#define lto_codegen_compile_optimized(...)                  LTO_WRAPPER_FUN(lto_codegen_compile_optimized, __VA_ARGS__)
#define lto_api_version(...)                                LTO_WRAPPER_FUN(lto_api_version, __VA_ARGS__)
#define lto_codegen_debug_options(...)                      LTO_WRAPPER_FUN(lto_codegen_debug_options, __VA_ARGS__)
#define lto_initialize_disassembler(...)                    LTO_WRAPPER_FUN(lto_initialize_disassembler, __VA_ARGS__)
#define lto_codegen_set_should_internalize(...)             LTO_WRAPPER_FUN(lto_codegen_set_should_internalize, __VA_ARGS__)
#define lto_codegen_set_should_embed_uselists(...)          LTO_WRAPPER_FUN(lto_codegen_set_should_embed_uselists, __VA_ARGS__)

#undef LIBLTO
#undef LTO_DECLARE
#undef LTO_LOOKUP

#endif // ! __APPLE__
