# Tools

The following tools are built and installed. Names are shown without any
configured target prefix.

## cctools

| Tool | Purpose |
| --- | --- |
| `ar` | Create and manage static archives. |
| `as` | Invoke Clang through a Mach-O assembler compatibility wrapper. |
| `bitcode_strip` | Remove embedded LLVM bitcode or retain only the bitcode from a Mach-O file. |
| `check_dylib` | Check a dynamic library's install name and preferred load address against a segment address table. |
| `checksyms` | Check Mach-O binaries for production conventions, including symbols, library versions and relocations. |
| `cmpdylib` | Compare dynamic libraries for architecture, exported-symbol and version compatibility. |
| `codesign_allocate` | Allocate space for code signatures in Mach-O files. |
| `ctf_insert` | Insert Compact C Type Format data into a Mach-O kernel binary. |
| `inout` | Read and rewrite Mach-O files and archives through libstuff. |
| `install_name_tool` | Change dynamic-library install names, dependencies and runtime search paths. |
| `libtool` | Create static archives or drive the creation of dynamic libraries (Apple libtool). |
| `lipo` | Create, inspect and extract architectures from universal binaries. |
| `makerelocs` | Generate PE/COFF base-relocation data from a Mach-O file. |
| `mtoc` | Convert a Mach-O EFI image to PE/COFF format. |
| `mtor` | Convert a fully linked Mach-O preload image to a raw binary. |
| `nm` | List symbols in object files, libraries and executables. |
| `nmedit` | Edit the visibility of symbols in Mach-O files. |
| `otool` | Inspect and disassemble Mach-O files. |
| `pagestuff` | Show how Mach-O structures, sections and code signatures map to file pages. |
| `ranlib` | Create or update the symbol index of a static archive. |
| `redo_prebinding` | Check or update prebinding information in older Mach-O binaries. |
| `seg_addr_table` | Manage preferred dynamic-library load addresses and detect address overlaps. |
| `seg_hack` | Rewrite Mach-O segment names, excluding debug sections. |
| `segedit` | Extract or replace Mach-O section contents. |
| `size` | Display segment and section sizes. |
| `strings` | Extract printable strings from files. |
| `strip` | Remove symbols and debugging information from Mach-O files. |
| `vtool` | Inspect and modify Mach-O build-version load commands. |

The classic `nm`, `size` and `otool` implementations are installed without the
`-classic` suffix used by the upstream Xcode targets.

### Tools retained from earlier cctools releases ###

Compared with Apple's [cctools-1035.1.102 source release](https://github.com/apple-oss-distributions/cctools/tree/cctools-1035.1.102),  
this port continues to build and install the following tools from `cctools 1030.6.3`  
for compatibility with existing workflows:

| Tool | Purpose |
| --- | --- |
| `checksyms` | Check Mach-O binaries for production conventions, including symbols, library versions and relocations. |
| `check_dylib` | Check a dynamic library's install name and preferred load address against a segment address table. |
| `cmpdylib` | Compare dynamic libraries for architecture, exported-symbol and version compatibility. |
| `pagestuff` | Show how Mach-O structures, sections and code signatures map to file pages. |
| `seg_addr_table` | Manage preferred dynamic-library load addresses and detect address overlaps. |
| `seg_hack` | Rewrite Mach-O segment names, excluding debug sections. |
| `redo_prebinding` | Check or update prebinding information in older Mach-O binaries. |
| `inout` | Read and rewrite Mach-O files and archives through libstuff. |

## ld64

| Tool | Purpose |
| --- | --- |
| `ld` | Link Mach-O executables, dynamic libraries, bundles and relocatable objects. |
| `ObjectDump` | Inspect object files using ld64's atom and fixup representation. |
| `unwinddump` | Display Mach-O unwind information. |
