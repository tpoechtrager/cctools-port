/*
 * Copyright © 2025 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1.  Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _COFF_H_
#define _COFF_H_

#include <stdint.h>

// fixed-size integer typedefs for EFI
typedef uint64_t  UINT64;
typedef int64_t   INT64;
typedef uint32_t  UINT32;
typedef int32_t   INT32;
typedef uint16_t  UINT16;
typedef uint16_t  CHAR16;
typedef int16_t   INT16;
typedef uint8_t   BOOLEAN;
typedef uint8_t   UINT8;
typedef char      CHAR8;
typedef int8_t    INT8;
typedef UINT64    UINTN;
typedef INT64     INTN;

#include "Base.h"

#include "PeImage.h"

// based on EFI_IMAGE_DOS_HEADER, COFF + DOS header
struct ms_dos_stub {
  UINT16  e_magic;    ///< Magic number.
  UINT16  e_cblp;     ///< Bytes on last page of file.
  UINT16  e_cp;       ///< Pages in file.
  UINT16  e_crlc;     ///< Relocations.
  UINT16  e_cparhdr;  ///< Size of header in paragraphs.
  UINT16  e_minalloc; ///< Minimum extra paragraphs needed.
  UINT16  e_maxalloc; ///< Maximum extra paragraphs needed.
  UINT16  e_ss;       ///< Initial (relative) SS value.
  UINT16  e_sp;       ///< Initial SP value.
  UINT16  e_csum;     ///< Checksum.
  UINT16  e_ip;       ///< Initial IP value.
  UINT16  e_cs;       ///< Initial (relative) CS value.
  UINT16  e_lfarlc;   ///< File address of relocation table.
  UINT16  e_ovno;     ///< Overlay number.
  UINT16  e_res[4];   ///< Reserved words.
  UINT16  e_oemid;    ///< OEM identifier (for e_oeminfo).
  UINT16  e_oeminfo;  ///< OEM information; e_oemid specific.
  UINT16  e_res2[10]; ///< Reserved words.
  UINT32  e_lfanew;   ///< File address of new exe header.
  char    dos_program[64]; ///<  MS-DOS ,stufa always follow DOS header.
};

// based on EFI_IMAGE_FILE_HEADER
struct filehdr {
  UINT16  f_magic;
  UINT16  f_nscns;
  UINT32  f_timdat;
  UINT32  f_symptr;
  UINT32  f_nsyms;
  UINT16  f_opthdr;
  UINT16  f_flags;
};

// based on EFI_IMAGE_OPTIONAL_HEADER32
struct aouthdr
{
    ///
    /// Standard fields.
    ///
    UINT16                    magic;
    UINT16                    vstamp;
    UINT32                    tsize;
    UINT32                    dsize;
    UINT32                    bsize;
    UINT32                    entry;
    UINT32                    text_start;
    UINT32                    data_start; ///< PE32 contains this additional field, which is absent in PE32+.
    ///
    /// Optional Header Windows-Specific Fields.
    ///
    UINT32                    ImageBase;
    UINT32                    SectionAlignment;
    UINT32                    FileAlignment;
    UINT16                    MajorOperatingSystemVersion;
    UINT16                    MinorOperatingSystemVersion;
    UINT16                    MajorImageVersion;
    UINT16                    MinorImageVersion;
    UINT16                    MajorSubsystemVersion;
    UINT16                    MinorSubsystemVersion;
    UINT32                    Win32VersionValue;
    UINT32                    SizeOfImage;
    UINT32                    SizeOfHeaders;
    UINT32                    CheckSum;
    UINT16                    Subsystem;
    UINT16                    DllCharacteristics;
    UINT32                    SizeOfStackReserve;
    UINT32                    SizeOfStackCommit;
    UINT32                    SizeOfHeapReserve;
    UINT32                    SizeOfHeapCommit;
    UINT32                    LoaderFlags;
    UINT32                    NumberOfRvaAndSizes;
    UINT32                    DataDirectory[EFI_IMAGE_NUMBER_OF_DIRECTORY_ENTRIES][2];
};

// based on EFI_IMAGE_OPTIONAL_HEADER64
struct aouthdr_64
{
    ///
    /// Standard fields.
    ///
    UINT16                    magic;
    UINT16                    vstamp;
    UINT32                    tsize;
    UINT32                    dsize;
    UINT32                    bsize;
    UINT32                    entry;
    UINT32                    text_start;
    ///
    /// Optional Header Windows-Specific Fields.
    ///
    UINT64                    ImageBase;
    UINT32                    SectionAlignment;
    UINT32                    FileAlignment;
    UINT16                    MajorOperatingSystemVersion;
    UINT16                    MinorOperatingSystemVersion;
    UINT16                    MajorImageVersion;
    UINT16                    MinorImageVersion;
    UINT16                    MajorSubsystemVersion;
    UINT16                    MinorSubsystemVersion;
    UINT32                    Win32VersionValue;
    UINT32                    SizeOfImage;
    UINT32                    SizeOfHeaders;
    UINT32                    CheckSum;
    UINT16                    Subsystem;
    UINT16                    DllCharacteristics;
    UINT64                    SizeOfStackReserve;
    UINT64                    SizeOfStackCommit;
    UINT64                    SizeOfHeapReserve;
    UINT64                    SizeOfHeapCommit;
    UINT32                    LoaderFlags;
    UINT32                    NumberOfRvaAndSizes;
    UINT32                    DataDirectory[EFI_IMAGE_NUMBER_OF_DIRECTORY_ENTRIES][2];
};

// based on EFI_IMAGE_SECTION_HEADER
struct scnhdr {
  char   s_name[EFI_IMAGE_SIZEOF_SHORT_NAME];
  UINT32 s_vsize;
  UINT32 s_vaddr;
  UINT32 s_size;
  UINT32 s_scnptr;
  UINT32 s_relptr;
  UINT32 s_lnnoptr;
  UINT16 s_nreloc;
  UINT16 s_nlnno;
  UINT32 s_flags;
};

// https://learn.microsoft.com/en-us/windows/win32/debug/pe-format?redirectedfrom=MSDN#coff-symbol-table
struct syment
{
  union
  {
    char short_name[EFI_IMAGE_SIZEOF_SHORT_NAME];

    struct
    {
      uint32_t zeros;
      uint32_t strtable_offset;
    } long_name;
  } name;

  uint32_t value;
  uint16_t scnum;
  uint16_t type;
  char     sclass;
  char     numaux;
} __attribute((packed,aligned(2)));

STATIC_ASSERT(sizeof(struct syment) == EFI_IMAGE_SIZEOF_SYMBOL, "symbol table record size mismatch");

/* Magic values that are true for all dos/nt implementations.  */
#define DOSMAGIC       0x5a4d

/* for the FileAlignment field */
#define FILEALIGNMENT  0x200   /* The alignment factor (in bytes) that is used
                                  to align the raw data of sections in the
                                  image file. The value should be a power of 2
                                  between 512 and 64 K, inclusive. The default
                                  is 512. If the SectionAlignment is less than
                                  the architecture's page size, then
                                  FileAlignment must match SectionAlignment. */

/* for the SectionAlignment field */
#define SECTIONALIGNMENT 0x1000 /* The alignment (in bytes) of sections when
                                  they are loaded into memory. It must be
                                  greater than or equal to FileAlignment.
                                  The default is the page size for the
                                  architecture. */

/* for the vstamp field */
#define LINKER_VERSION 256 /* That is, 2.56 */
/* This piece of magic sets the "linker version" field to LINKER_VERSION.  */
#define VSTAMP (LINKER_VERSION / 100 + (LINKER_VERSION % 100) * 256)

#define IMAGE_FILE_MACHINE_ARM   0x01C0
#define IMAGE_FILE_MACHINE_AMD64 0x8664

#endif
