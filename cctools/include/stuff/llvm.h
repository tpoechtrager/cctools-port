#ifndef _STUFF_LLVM_H_
#define _STUFF_LLVM_H_

#include "llvm-c/Disassembler.h"

extern LLVMDisasmContextRef llvm_create_disasm(
    const char *TripleName,
    const char *CPU,
    void *DisInfo,
    int TagType,
    LLVMOpInfoCallback GetOpInfo,
    LLVMSymbolLookupCallback SymbolLookUp);

extern void llvm_disasm_dispose(
    LLVMDisasmContextRef DC);

extern size_t llvm_disasm_instruction(
    LLVMDisasmContextRef DC,
    uint8_t *Bytes,
    uint64_t BytesSize,
    uint64_t Pc,
    char *OutString,
    size_t OutStringSize);

__private_extern__ int llvm_disasm_set_options(
    LLVMDisasmContextRef DC,
    uint64_t Options);

__private_extern__ const char *llvm_disasm_version_string(
    void);

#endif /* _STUFF_LLVM_H_ */
