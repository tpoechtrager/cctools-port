#ifndef _STUFF_LLVM_H_
#define _STUFF_LLVM_H_

#include "llvm-c/Disassembler.h"

extern LLVMDisasmContextRef llvm_create_disasm(
    const char *TripleName,
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

#endif /* _STUFF_LLVM_H_ */
