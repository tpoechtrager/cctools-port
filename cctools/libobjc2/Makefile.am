noinst_LTLIBRARIES = libobjc.la

libobjc_la_CPPFLAGS= -DTYPE_DEPENDENT_DISPATCH -DGNUSTEP -D__OBJC_RUNTIME_INTERNAL__=1 -D_XOPEN_SOURCE=500 -D__BSD_VISIBLE=1 -D_DEFAULT_SOURCE=1 -DNO_SELECTOR_MISMATCH_WARNINGS

libobjc_la_CFLAGS = -std=gnu99 -fPIC -fexceptions $(WARNINGS)

libobjc_la_OBJCFLAGS = $(OBJCWARNINGS)

libobjc_la_SOURCES =  \
    NSBlocks.m\
    Protocol2.m\
    abi_version.c\
    alias_table.c\
    arc.m\
    associate.m\
    blocks_runtime.m\
    block_to_imp.c\
    block_trampolines.S\
    objc_msgSend.S\
    caps.c\
    category_loader.c\
    class_table.c\
    dtable.c\
    eh_personality.c\
    encoding2.c\
    gc_none.c\
    hash_table.c\
    hooks.c\
    ivar.c\
    legacy_malloc.c\
    loader.c\
    mutation.m\
    properties.m\
    protocol.c\
    runtime.c\
    sarray2.c\
    selector_table.c\
    sendmsg2.c\
    statics_loader.c\
    toydispatch.c
