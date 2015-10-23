/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#if defined(HAVE_XAR_XAR_H) && defined(LTO_SUPPORT) // ld64-port

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <vector>
#include <dlfcn.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <unordered_map>

#include "llvm-c/lto.h"
// c header
extern "C" {
#include <xar/xar.h>
}

#include "bitcode_bundle.h"

#include "Options.h"
#include "ld.hpp"
#include "Bitcode.hpp"
#include "macho_relocatable_file.h"


namespace ld {
namespace passes {
namespace bitcode_bundle {

class BitcodeTempFile;

class BitcodeAtom : public ld::Atom {
    static ld::Section                      bitcodeBundleSection;
public:
                                            BitcodeAtom();
                                            BitcodeAtom(BitcodeTempFile& tempfile);
                                            ~BitcodeAtom()                  { free(_content); }
    virtual ld::File*						file() const					{ return NULL; }
    virtual const char*						name() const					{ return "bitcode bundle"; }
    virtual uint64_t						size() const					{ return _size; }
    virtual uint64_t						objectAddress() const			{ return 0; }
    virtual void							copyRawContent(uint8_t buffer[]) const
                                                                            { memcpy(buffer, _content, _size); }
    virtual void							setScope(Scope)					{ }

private:
    uint8_t*                                _content;
    uint64_t								_size;
};

ld::Section BitcodeAtom::bitcodeBundleSection("__LLVM", "__bundle", ld::Section::typeSectCreate);

class BitcodeTempFile {
public:
                                            BitcodeTempFile(const char* path, bool deleteAfterRead);
                                            ~BitcodeTempFile();
    uint8_t*                                getContent() const              { return _content; }
    uint64_t                                getSize() const                 { return _size; }
private:
    friend class BitcodeAtom;
    const char* _path;
    uint8_t* _content;
    uint64_t _size;
    bool _deleteAfterRead;
};

class BitcodeObfuscator {
public:
    BitcodeObfuscator();
    ~BitcodeObfuscator();

    void addMustPreserveSymbols(const char* name);
    void bitcodeHideSymbols(ld::Bitcode* bc, const char* filePath, const char* outputPath);
    void writeSymbolMap(const char* outputPath);
private:
    typedef void (*lto_codegen_func_t) (lto_code_gen_t);
    typedef void (*lto_codegen_output_t) (lto_code_gen_t, const char*);

    lto_code_gen_t                          _obfuscator;
    lto_codegen_func_t                      _lto_hide_symbols;
    lto_codegen_func_t                      _lto_reset_context;
    lto_codegen_output_t                    _lto_write_reverse_map;
};

class FileHandler {
    // generic handler for files in a bundle
public:
    virtual void populateMustPreserveSymbols(BitcodeObfuscator* _obfuscator)    { }
    virtual void obfuscateAndWriteToPath(BitcodeObfuscator* _obfuscator, const char* path) { };
    xar_file_t getXARFile()                 { return _xar_file; }

    FileHandler(char* content, size_t size) :
        _parent(NULL), _xar_file(NULL), _file_buffer(content), _file_size(size) {   }           // eager construct
    FileHandler(xar_t parent, xar_file_t xar_file) :
        _parent(parent), _xar_file(xar_file), _file_buffer(NULL), _file_size(0) {   }           // lazy construct
    virtual ~FileHandler()                                                      {   }

protected:
    void initFile() {
        if (!_file_buffer) {
            if (xar_extract_tobuffersz(_parent, _xar_file, &_file_buffer, &_file_size) != 0)
                throwf("could not extract files from bitcode bundle");
        }
    }
    void destroyFile() {
        if (_parent)
            free(_file_buffer);
    }

    xar_t                               _parent;
    xar_file_t                          _xar_file;
    char*                               _file_buffer;
    size_t                              _file_size;
};

class BundleHandler : public FileHandler {
public:
    BundleHandler(char* bundleContent, size_t bundleSize, const Options& options) :
        FileHandler(bundleContent, bundleSize), _xar(NULL), _temp_dir(NULL), _options(options) { }
    BundleHandler(xar_t parent, xar_file_t xar_file, const Options& options) :
        FileHandler(parent, xar_file), _xar(NULL), _temp_dir(NULL), _options(options) { }

    ~BundleHandler();

    virtual void populateMustPreserveSymbols(BitcodeObfuscator* obfuscator) override;
    virtual void obfuscateAndWriteToPath(BitcodeObfuscator* obfuscator, const char* path) override;

private:
    void init();
    void copyXARProp(xar_file_t src, xar_file_t dst);

    xar_t                                   _xar;
    char*                                   _temp_dir;
    const Options&                          _options;
    std::vector<FileHandler*>               _handlers;
};

class BitcodeHandler : public FileHandler {
public:
    BitcodeHandler(char* content, size_t size) : FileHandler(content, size)      { }
    BitcodeHandler(xar_t parent, xar_file_t xar_file) : FileHandler(parent, xar_file)   { }

    ~BitcodeHandler();

    virtual void populateMustPreserveSymbols(BitcodeObfuscator* obfuscator) override { } // Don't need to preserve symbols
    virtual void obfuscateAndWriteToPath(BitcodeObfuscator* obfuscator, const char* path) override;
};

class ObjectHandler : public FileHandler {
public:
    ObjectHandler(char* content, size_t size) :
        FileHandler(content, size)                      { }
    ObjectHandler(xar_t parent, xar_file_t xar_file) :
        FileHandler(parent, xar_file)                   { }

    ~ObjectHandler();

    void populateMustPreserveSymbols(BitcodeObfuscator* obfuscator) override;
    void obfuscateAndWriteToPath(BitcodeObfuscator* obfuscator, const char* path) override;

};


class BitcodeBundle {
public:
    BitcodeBundle(const Options& opts, ld::Internal& internal) :
        _options(opts), _state(internal)    { }
    ~BitcodeBundle()                        { }
    void                                    doPass();

private:
    const Options&                          _options;
    ld::Internal&                           _state;
};

BitcodeAtom::BitcodeAtom()
: ld::Atom(bitcodeBundleSection,
           ld::Atom::definitionRegular, ld::Atom::combineNever,
           ld::Atom::scopeTranslationUnit, ld::Atom::typeUnclassified,
           ld::Atom::symbolTableNotIn, true, false, false, ld::Atom::Alignment(0)),
    _size(1)
{
    // initialize a marker of 1 byte
    _content = (uint8_t*)calloc(1,1);
}

BitcodeAtom::BitcodeAtom(BitcodeTempFile& tempfile)
    : ld::Atom(bitcodeBundleSection,
               ld::Atom::definitionRegular, ld::Atom::combineNever,
               ld::Atom::scopeTranslationUnit, ld::Atom::typeUnclassified,
               ld::Atom::symbolTableNotIn, true, false, false, ld::Atom::Alignment(0)),
    _content(tempfile._content), _size(tempfile._size)
{
    // Creating the Atom will transfer the ownership of the buffer from Tempfile to Atom
    tempfile._content = NULL;
}

BitcodeTempFile::BitcodeTempFile(const char* path, bool deleteAfterRead = true)
    : _path(path), _deleteAfterRead(deleteAfterRead)
{
    int fd = ::open(path, O_RDONLY, 0);
    if ( fd == -1 )
        throwf("could not open bitcode temp file: %s", path);
    struct stat stat_buf;
    ::fstat(fd, &stat_buf);
    _content = (uint8_t*)malloc(stat_buf.st_size);
    if ( _content == NULL )
        throwf("could not process bitcode temp file: %s", path);
    if ( read(fd, _content, stat_buf.st_size) != stat_buf.st_size )
        throwf("could not read bitcode temp file: %s", path);
    ::close(fd);
    _size = stat_buf.st_size;
}

BitcodeTempFile::~BitcodeTempFile()
{
    free(_content);
    if ( _deleteAfterRead ) {
        if ( ::unlink(_path) != 0 )
            throwf("could not remove temp file: %s", _path);
    }
}

BitcodeObfuscator::BitcodeObfuscator()
{
    // check if apple internal libLTO is used
    if ( ::lto_get_version() == NULL )
        throwf("libLTO is not loaded");
    _lto_hide_symbols = (lto_codegen_func_t) dlsym(RTLD_DEFAULT, "lto_codegen_hide_symbols");
    _lto_write_reverse_map = (lto_codegen_output_t) dlsym(RTLD_DEFAULT, "lto_codegen_write_symbol_reverse_map");
    _lto_reset_context = (lto_codegen_func_t) dlsym(RTLD_DEFAULT, "lto_codegen_reset_context");
    if ( _lto_hide_symbols == NULL || _lto_write_reverse_map == NULL ||
        _lto_reset_context == NULL || ::lto_api_version() < 14 )
        throwf("loaded libLTO doesn't support -bitcode_hide_symbols: %s", ::lto_get_version());
    _obfuscator = ::lto_codegen_create_in_local_context();
#if LTO_API_VERSION >= 14
    lto_codegen_set_should_internalize(_obfuscator, false);
#endif
}

BitcodeObfuscator::~BitcodeObfuscator()
{
    ::lto_codegen_dispose(_obfuscator);
}

void BitcodeObfuscator::addMustPreserveSymbols(const char* name)
{
    ::lto_codegen_add_must_preserve_symbol(_obfuscator, name);
}

void BitcodeObfuscator::bitcodeHideSymbols(ld::Bitcode* bc, const char* filePath, const char* outputPath)
{
#if LTO_API_VERSION >= 13 && LTO_APPLE_INTERNAL
    lto_module_t module = ::lto_module_create_in_codegen_context(bc->getContent(), bc->getSize(), filePath, _obfuscator);
    if ( module == NULL )
        throwf("object contains invalid bitcode: %s", filePath);
    ::lto_codegen_set_module(_obfuscator, module);
    (*_lto_hide_symbols)(_obfuscator);
#if LTO_API_VERSION >= 15
    ::lto_codegen_set_should_embed_uselists(_obfuscator, true);
#endif
    ::lto_codegen_write_merged_modules(_obfuscator, outputPath);
    (*_lto_reset_context)(_obfuscator);
#endif
    return;
}

void BitcodeObfuscator::writeSymbolMap(const char *outputPath)
{
    (*_lto_write_reverse_map)(_obfuscator, outputPath);
}

BundleHandler::~BundleHandler()
{
    // free buffers
    destroyFile();
    // free handlers
    for (auto handler : _handlers)
        delete handler;

    // delete temp file if not -save-temps
    if ( _xar ) {
        xar_close(_xar);
        std::string oldXARPath = std::string(_temp_dir) + std::string("/bundle.xar");
        if ( !_options.saveTempFiles() && ::unlink(oldXARPath.c_str()) != 0)
            warning("could not delete temp file: %s", oldXARPath.c_str());
    }

    if ( _temp_dir ) {
        if ( !_options.saveTempFiles() && ::rmdir(_temp_dir) != 0 )
            warning("could not delete temp directory: %s", _temp_dir);
        free(_temp_dir);
    }
}

BitcodeHandler::~BitcodeHandler()
{
    destroyFile();
}

ObjectHandler::~ObjectHandler()
{
    destroyFile();
}

void BundleHandler::init()
{
    if ( _xar != NULL )
        return;

    // make temp directory
    const char* finalOutput = _options.outputFilePath();
    _temp_dir = (char*)malloc(PATH_MAX * sizeof(char));
    // Check outputFilePath.bundle-XXXXXX/YYYYYYYYYY.bc will not over flow PATH_MAX
    // If so, fall back to /tmp
    if ( strlen(finalOutput) + 30 >= PATH_MAX )
        sprintf(_temp_dir, "/tmp/ld.bundle.XXXXXX");
    else
        sprintf(_temp_dir, "%s.bundle.XXXXXX", finalOutput);
    ::mkdtemp(_temp_dir);

    // write the bundle to the temp_directory
    initFile();
    std::string oldXARPath = std::string(_temp_dir) + std::string("/bundle.xar");
    int f = ::open(oldXARPath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if ( f == -1 )
        throwf("could not write file to temp directory: %s", _temp_dir);
    if ( ::write(f, _file_buffer, _file_size) != (int)_file_size )
        throwf("failed to write content to temp file: %s", oldXARPath.c_str());
    ::close(f);

    // read the xar file
    _xar = xar_open(oldXARPath.c_str(), READ);

    // Init the vector of handler
    xar_iter_t iter = xar_iter_new();
    if ( !iter )
        throwf("could not aquire iterator for the bitcode bundle");
    for ( xar_file_t f = xar_file_first(_xar, iter); f; f = xar_file_next(iter) ) {
        const char* filetype = NULL;
        if ( xar_prop_get(f, "file-type", &filetype) != 0 )
            throwf("could not get the file type for the bitcode bundle");
        if ( strcmp(filetype, "Bundle") == 0 )
            _handlers.push_back(new BundleHandler(_xar, f, _options));
        else if ( strcmp(filetype, "Object") == 0 )
            _handlers.push_back(new ObjectHandler(_xar, f));
        else if ( strcmp(filetype, "Bitcode") == 0 || strcmp(filetype, "LTO") == 0 )
            _handlers.push_back(new BitcodeHandler(_xar, f));
        else
            assert(0 && "Unknown file type");
    }
    xar_iter_free(iter);
}

void BundleHandler::copyXARProp(xar_file_t src, xar_file_t dst)
{
    // copy the property in the XAR.
    // Since XAR API can only get the first value from the key,
    // Deleting the value after read.
    int i = 0;
    while (1) {
        xar_iter_t p = xar_iter_new();
        const char* key = xar_prop_first(src, p);
        for (int x = 0; x < i; x++)
            key = xar_prop_next(p);
        if ( !key )
            break;
        const char* val = NULL;
        xar_prop_get(src, key, &val);
        if ( // Info from bitcode files
             strcmp(key, "file-type") == 0 ||
             strcmp(key, "clang/cmd") == 0 ||
             strcmp(key, "swift/cmd") == 0 ||
             // Info from linker subdoc
             strcmp(key, "version") == 0 ||
             strcmp(key, "architecture") == 0 ||
             strcmp(key, "hide-symbols") == 0 ||
             strcmp(key, "platform") == 0 ||
             strcmp(key, "sdkversion") == 0 ||
             strcmp(key, "dylibs/lib") == 0 ||
             strcmp(key, "link-options/option") == 0 ) {
            xar_prop_create(dst, key, val);
            xar_prop_unset(src, key);
        } else
            ++ i;
        xar_iter_free(p);
    }
}

void BundleHandler::populateMustPreserveSymbols(BitcodeObfuscator* obfuscator)
{
    // init the handler
    if ( _xar == NULL )
        init();

    // iterate through the XAR file and add symbols
    for ( auto handler : _handlers )
        handler->populateMustPreserveSymbols(obfuscator);
}

void ObjectHandler::populateMustPreserveSymbols(BitcodeObfuscator* obfuscator)
{
    initFile();
    // Parse the object file and add the symbols
    std::vector<const char*> symbols;
    if ( mach_o::relocatable::getNonLocalSymbols((uint8_t*)_file_buffer, symbols) ) {
        for ( auto sym : symbols )
            obfuscator->addMustPreserveSymbols(sym);
    }
}

void BundleHandler::obfuscateAndWriteToPath(BitcodeObfuscator *obfuscator, const char *path)
{
    // init the handler
    if ( _xar == NULL )
        init();

    // creating the new xar
    xar_t x = xar_open(path, WRITE);
    if (x == NULL)
        throwf("could not open output bundle to write %s", path);
    // Disable compression
    if (xar_opt_set(x, XAR_OPT_COMPRESSION, XAR_OPT_VAL_NONE) != 0)
        throwf("could not disable compression for bitcode bundle");

    // iterate through the XAR file and obfuscate
    for ( auto handler : _handlers ) {
        const char* name = NULL;
        xar_file_t f = handler->getXARFile();
        if ( xar_prop_get(f, "name", &name) != 0 )
            throwf("could not get the name of the file from bitcode bundle");
        char outputPath[PATH_MAX];
        sprintf(outputPath, "%s/%s", _temp_dir, name);
        handler->obfuscateAndWriteToPath(obfuscator, outputPath);
        BitcodeTempFile* bcOut = new BitcodeTempFile(outputPath, !_options.saveTempFiles());
        xar_file_t bcEntry = xar_add_frombuffer(x, NULL, name, (char*)bcOut->getContent(), bcOut->getSize());
        copyXARProp(f, bcEntry);
        delete bcOut;
    }

    // copy the subdoc as well
    for ( xar_subdoc_t sub = xar_subdoc_first(_xar); sub; sub = xar_subdoc_next(sub) ) {
        const char *name = xar_subdoc_name(sub);
        xar_subdoc_t newDoc = xar_subdoc_new(x, name);
        copyXARProp((xar_file_t) sub, (xar_file_t) newDoc);
    }
    xar_close(x);
}

void BitcodeHandler::obfuscateAndWriteToPath(BitcodeObfuscator *obfuscator, const char *path)
{
    initFile();
    ld::Bitcode bc((uint8_t*)_file_buffer, _file_size);
    obfuscator->bitcodeHideSymbols(&bc, path, path);
}

void ObjectHandler::obfuscateAndWriteToPath(BitcodeObfuscator *obfuscator, const char *path)
{
    initFile();
    int f = ::open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if ( f == -1 || ::write(f, _file_buffer, _file_size) != (int)_file_size )
        throwf("failed to write content to temp file: %s", path);
    ::close(f);
}

void BitcodeBundle::doPass()
{
    if ( _state.embedMarkerOnly ) {
        assert( _options.outputKind() != Options::kDynamicExecutable &&
                _options.outputKind() != Options::kStaticExecutable &&
                "Don't emit marker for executables");
        BitcodeAtom* marker = new BitcodeAtom();
        _state.addAtom(*marker);
        return;
    }

    if ( _state.filesWithBitcode.empty() && _state.ltoBitcodePath.empty() )
        return;
    // Create tempdir, the temp directory should be OUTPUT/main.exe.bundle-XXXXXX
    char tempdir[PATH_MAX];
    const char* finalOutput = _options.outputFilePath();
    // Check outputFilePath.bundle-XXXXXX/YYYYYYYYYY.bc will not over flow PATH_MAX
    // If so, fall back to /tmp
    if ( strlen(finalOutput) + 30 >= PATH_MAX )
        sprintf(tempdir, "/tmp/ld.bundle.XXXXXX");
    else
        sprintf(tempdir, "%s.bundle.XXXXXX", finalOutput);
    ::mkdtemp(tempdir);
    // A lookup map to look for BundlerHandler base on filename
    std::unordered_map<std::string, BundleHandler*> handlerMap;

    BitcodeObfuscator* obfuscator = _options.hideSymbols() ? new BitcodeObfuscator() : NULL;
    // Build must keep symbols if we need to hide all the symbols
    if ( _options.hideSymbols() ) {
        // Go through all the atoms and decide if it should be obfuscated.
        // The following symbols are kept:
        // 1. entry point
        // 2. undefined symbols
        // 3. symbols must not be stripped
        // 4. all the globals if the globals are dead_strip root (ex. dylibs)
        // 5. there is an exported symbol list suggests the symbol should be exported
        // 6. the special symbols supplied by linker
        for ( auto &sect : _state.sections ) {
            for ( auto &atom : sect->atoms ) {
                if ( atom == _state.entryPoint ||
                     atom->definition() == ld::Atom::definitionProxy ||
                     atom->symbolTableInclusion() == ld::Atom::symbolTableInAndNeverStrip ||
                     ( _options.allGlobalsAreDeadStripRoots() && atom->scope() == ld::Atom::scopeGlobal ) ||
                     ( _options.hasExportRestrictList() && _options.shouldExport(atom->name())) )
                    obfuscator->addMustPreserveSymbols(atom->name());
            }
        }
        // If there are assembly sources, add globals and undefined symbols from them as well
        for ( auto &f : _state.filesWithBitcode ) {
            if ( ld::AsmBitcode* ab = dynamic_cast<ld::AsmBitcode*>(f->getBitcode()) ) {
                ObjectHandler objHandler((char*)ab->getContent(), ab->getSize());
                objHandler.populateMustPreserveSymbols(obfuscator);
            } else if ( ld::BundleBitcode* bb = dynamic_cast<ld::BundleBitcode*>(f->getBitcode()) ) {
                BundleHandler* bh = new BundleHandler((char*)bb->getContent(), bb->getSize(), _options);
                bh->populateMustPreserveSymbols(obfuscator);
                handlerMap.emplace(std::string(f->path()), bh);
            }
        }
        // special symbols supplied by linker
        obfuscator->addMustPreserveSymbols("___dso_handle");
        obfuscator->addMustPreserveSymbols("__mh_execute_header");
        obfuscator->addMustPreserveSymbols("__mh_dylib_header");
        obfuscator->addMustPreserveSymbols("__mh_bundle_header");
        obfuscator->addMustPreserveSymbols("__mh_dylinker_header");
        obfuscator->addMustPreserveSymbols("__mh_object_header");
        obfuscator->addMustPreserveSymbols("__mh_preload_header");
    }

    // Open XAR output
    xar_t x;
    char outFile[PATH_MAX];
    sprintf(outFile, "%s/bundle.xar", tempdir);

    // By default, it uses gzip to compress and SHA1 as checksum
    x = xar_open(outFile, WRITE);
    if (x == NULL)
        throwf("could not open output bundle to write %s", outFile);
    // Disable compression
    if (xar_opt_set(x, XAR_OPT_COMPRESSION, XAR_OPT_VAL_NONE) != 0)
        throwf("could not disable compression for bitcode bundle");

    // Sort all the object file according to oridnal order
    std::sort(_state.filesWithBitcode.begin(), _state.filesWithBitcode.end(),
              [](const ld::relocatable::File* a, const ld::relocatable::File* b) {
                  return a->ordinal() < b->ordinal();
              });

    // Copy each bitcode file into archive
    int index = 1;
    char formatString[10];
    sprintf(formatString, "%%0%ud", (unsigned int)log10(_state.filesWithBitcode.size()) + 1);
    for ( auto &obj : _state.filesWithBitcode ) {
        assert(obj->getBitcode() != NULL && "File should contain bitcode");
        char outFilePath[16];
        sprintf(outFilePath, formatString, index++);
        if ( ld::LLVMBitcode* llvmbc = dynamic_cast<ld::LLVMBitcode*>(obj->getBitcode()) ) {
            // Handle clang and swift bitcode
            xar_file_t bcFile = NULL;
            if ( _options.hideSymbols() && !llvmbc->isMarker() ) { // dont strip if it is just a marker
                char tempfile[PATH_MAX];
                sprintf(tempfile, "%s/%s.bc", tempdir, outFilePath);
                obfuscator->bitcodeHideSymbols(llvmbc, obj->path(), tempfile);
                BitcodeTempFile* bcTemp = new BitcodeTempFile(tempfile, !_options.saveTempFiles());
                bcFile = xar_add_frombuffer(x, NULL, outFilePath, (char*)bcTemp->getContent(), bcTemp->getSize());
                delete bcTemp;
            } else {
                bcFile = xar_add_frombuffer(x, NULL, outFilePath, (char*)const_cast<uint8_t*>(llvmbc->getContent()), llvmbc->getSize());
            }
            if ( bcFile == NULL )
                throwf("could not add bitcode from %s to bitcode bundle", obj->path());
            if ( xar_prop_set(bcFile, "file-type", "Bitcode") != 0 )
                throwf("could not set bitcode property for %s in bitcode bundle", obj->path());
            // Write commandline options
            std::string tagName = std::string(llvmbc->getBitcodeName()) + std::string("/cmd");
            for ( uint32_t i = 0; i < llvmbc->getCmdSize(); ++i ) {
                if ( i == 0 || llvmbc->getCmdline()[i-1] == '\0' ) {
                    if ( xar_prop_create(bcFile, tagName.c_str(), (const char *)llvmbc->getCmdline() + i) )
                        throwf("could not set cmdline to XAR file");
                }
            }
        }
        else if ( ld::BundleBitcode* bundlebc = dynamic_cast<ld::BundleBitcode*>(obj->getBitcode()) ) {
            xar_file_t bundleFile = NULL;
            if ( _options.hideSymbols() && !bundlebc->isMarker() ) { // dont strip if it is just a marker
                char tempfile[PATH_MAX];
                sprintf(tempfile, "%s/%s.xar", tempdir, outFilePath);
                auto search = handlerMap.find(std::string(obj->path()));
                assert( search != handlerMap.end() && "Cannot find handler");
                search->second->obfuscateAndWriteToPath(obfuscator, tempfile);
                BitcodeTempFile* bundleTemp = new BitcodeTempFile(tempfile, !_options.saveTempFiles());
                bundleFile = xar_add_frombuffer(x, NULL, outFilePath, (char*)bundleTemp->getContent(), bundleTemp->getSize());
                delete bundleTemp;
            } else {
                bundleFile = xar_add_frombuffer(x, NULL, outFilePath,
                                                (char*)const_cast<uint8_t*>(bundlebc->getContent()),
                                                bundlebc->getSize());
            }
            if ( bundleFile == NULL )
                throwf("could not add bitcode from the bundle %s to bitcode bundle", obj->path());
            if ( xar_prop_set(bundleFile, "file-type", "Bundle") != 0 )
                throwf("could not set bundle property for %s in bitcode bundle", obj->path());
        }
        else if ( ld::AsmBitcode* asmbc = dynamic_cast<ld::AsmBitcode*>(obj->getBitcode()) ) {
            xar_file_t objFile = xar_add_frombuffer(x, NULL, outFilePath, (char*)asmbc->getContent(), asmbc->getSize());
            if ( objFile == NULL )
                throwf("could not add obj file %s to bitcode bundle", obj->path());
            if ( xar_prop_set(objFile, "file-type", "Object") != 0 )
                throwf("could not set object property for %s in bitcode bundle", obj->path());
        }
        else {
            assert(false && "Unknown bitcode");
        }
    }

    // Write merged LTO bitcode
    if ( !_state.ltoBitcodePath.empty() ) {
        xar_file_t ltoFile = NULL;
        BitcodeTempFile* ltoTemp = new BitcodeTempFile(_state.ltoBitcodePath.c_str(), !_options.saveTempFiles());
        if ( _options.hideSymbols() ) {
            ld::Bitcode ltoBitcode(ltoTemp->getContent(), ltoTemp->getSize());
            char ltoTempFile[PATH_MAX];
            sprintf(ltoTempFile, "%s/lto.bc", tempdir);
            obfuscator->bitcodeHideSymbols(&ltoBitcode, _state.ltoBitcodePath.c_str(), ltoTempFile);
            BitcodeTempFile* ltoStrip = new BitcodeTempFile(ltoTempFile, !_options.saveTempFiles());
            ltoFile = xar_add_frombuffer(x, NULL, "lto.o", (char*)ltoStrip->getContent(), ltoStrip->getSize());
            delete ltoStrip;
        } else {
            ltoFile = xar_add_frombuffer(x, NULL, "lto.o", (char*)ltoTemp->getContent(), ltoTemp->getSize());
        }
        if ( ltoFile == NULL )
            throwf("could not add lto file %s to bitcode bundle", _state.ltoBitcodePath.c_str());
        if ( xar_prop_set(ltoFile, "file-type", "LTO") != 0 )
            throwf("could not set bitcode property for %s in bitcode bundle", _state.ltoBitcodePath.c_str());
        delete ltoTemp;
    }

    // Common LinkOptions
    std::vector<std::string> linkCmd = _options.writeBitcodeLinkOptions();

    // support -sectcreate option
    for ( auto extraSect = _options.extraSectionsBegin(); extraSect != _options.extraSectionsEnd(); ++ extraSect ) {
        std::string sectName = std::string(extraSect->segmentName) + std::string(",") + std::string(extraSect->sectionName);
        BitcodeTempFile* sectFile = new BitcodeTempFile(extraSect->path, false);
        xar_file_t sectXar = xar_add_frombuffer(x, NULL, sectName.c_str(), (char*)sectFile->getContent(), sectFile->getSize());
        if ( sectXar == NULL )
            throwf("could not encode sectcreate file %s into bitcode bundle", extraSect->path);
        if ( xar_prop_set(sectXar, "file-type", "Section") != 0 )
            throwf("could not set bitcode property for %s", sectName.c_str());
        delete sectFile;
        linkCmd.push_back("-sectcreate");
        linkCmd.push_back(extraSect->segmentName);
        linkCmd.push_back(extraSect->sectionName);
        linkCmd.push_back(sectName);
    }

    // Write exports file
    if ( _options.hasExportMaskList() ) {
        linkCmd.push_back("-exported_symbols_list");
        linkCmd.push_back("exports.exp");
        const char* exportsPath = "exports.exp";
        std::vector<const char*> exports = _options.exportsData();
        std::string exps;
        for (std::vector<const char*>::iterator it = exports.begin();
             it != exports.end(); ++ it) {
            exps += *it;
            exps += "\n";
        }
        // always append an empty line so exps cannot be empty. rdar://problem/22404253
        exps += "\n";
        xar_file_t exportsFile = xar_add_frombuffer(x, NULL, exportsPath, const_cast<char*>(exps.data()), exps.size());
        if (exportsFile == NULL)
            throwf("could not add exports list to bitcode bundle");
        if (xar_prop_set(exportsFile, "file-type", "Exports") != 0)
            throwf("could not set exports property in bitcode bundle");
    }

    // Create subdoc to write link information
    xar_subdoc_t linkXML = xar_subdoc_new(x, "Ld");
    if ( linkXML == NULL )
        throwf("could not create XML in bitcode bundle");

    // Write version number
    if ( xar_prop_create((xar_file_t)linkXML, "version", BITCODE_XAR_VERSION) != 0 )
        throwf("could not add version number to bitcode bundle");

    // Arch
    if ( xar_prop_create((xar_file_t)linkXML, "architecture", _options.architectureName()) != 0 )
        throwf("could not add achitecture name to bitcode bundle");

    // Opt-out symbols
    if ( _options.hideSymbols() ) {
        if ( xar_prop_create((xar_file_t)linkXML, "hide-symbols", "1") != 0 )
            throwf("could not add property to bitcode bundle");
    }

    // Write SDK version
    if ( _options.sdkPaths().size() > 1 )
        throwf("only one -syslibroot is accepted for bitcode bundle");
    if ( xar_prop_create((xar_file_t)linkXML, "platform", _options.getPlatformStr().c_str()) != 0 )
        throwf("could not add platform name to bitcode bundle");
    if ( xar_prop_create((xar_file_t)linkXML, "sdkversion", _options.getSDKVersionStr().c_str()) != 0 )
        throwf("could not add SDK version to bitcode bundle");

    // Write dylibs
    const char* sdkRoot = NULL;
    if ( !_options.sdkPaths().empty() )
        sdkRoot = _options.sdkPaths().front();
    if ( !_state.dylibs.empty() ) {
        std::vector<const char*> SDKPaths = _options.sdkPaths();
        char dylibPath[PATH_MAX];
        for ( auto &dylib : _state.dylibs ) {
            // For every dylib/framework, figure out if it is coming from a SDK
            // if it is coming from some SDK, we parse the path to figure out which SDK
            // If -syslibroot is pointing to a SDK, it should end with PlatformX.Y.sdk/
            if (sdkRoot && strncmp(dylib->path(), sdkRoot, strlen(sdkRoot)) == 0) {
                // dylib/framework from one of the -syslibroot
                // The path start with a string template
                strcpy(dylibPath, "{SDKPATH}/");
                // append the path of dylib/frameowrk in the SDK
                strcat(dylibPath, dylib->path() + strlen(sdkRoot));
            } else {
                // Not in any SDKs, then assume it is a user dylib/framework
                // strip off all the path in the front
                const char* dylib_name = strrchr(dylib->path(), '/');
                dylib_name = (dylib_name == NULL) ? dylib->path() : dylib_name + 1;
                strcpy(dylibPath, dylib_name);
            }
            if ( dylib->forcedWeakLinked() ) {
                if ( xar_prop_create((xar_file_t)linkXML, "dylibs/weak", dylibPath) != 0)
                    throwf("could not add dylib options to bitcode bundle");
            } else {
                if ( xar_prop_create((xar_file_t)linkXML, "dylibs/lib", dylibPath) != 0)
                    throwf("could not add dylib options to bitcode bundle");
            }
        }
    }

    // Write link-line into archive
    for ( auto &it : linkCmd ) {
        if (xar_prop_create((xar_file_t)linkXML, "link-options/option", it.c_str()) != 0)
            throwf("could not add link options to bitcode bundle");
    }
    // Finish writing
    xar_close(x);

    // Read the file back
    BitcodeTempFile* xarTemp = new BitcodeTempFile(outFile, !_options.saveTempFiles());

    // Create an Atom and add to the list
    BitcodeAtom* bundleAtom = new BitcodeAtom(*xarTemp);
    _state.addAtom(*bundleAtom);

    // write the reverse mapping file if required
    if ( _options.hideSymbols() && !_options.reverseMapTempPath().empty() )
        obfuscator->writeSymbolMap(_options.reverseMapTempPath().c_str());

    // Clean up local variables
    delete xarTemp;
    delete obfuscator;
    for ( auto &entry: handlerMap )
        delete entry.second;
    // delete temp directory if not using -save-temps
    // only do so after all the BitcodeTempFiles are deleted.
    if ( !_options.saveTempFiles() ) {
        if ( ::rmdir(tempdir) != 0 )
            warning("temp directory cannot be removed: %s", tempdir);
    }
}



// called by linker to write bitcode bundle into a mach-o section
void doPass(const Options& opts, ld::Internal& internal) {
    BitcodeBundle BB(opts, internal);
    BB.doPass();
}


} // namespace bitcode_bundle
} // namespace passes
} // namespace ld

#endif // HAVE_XAR_XAR_H && LTO_SUPPORT 
