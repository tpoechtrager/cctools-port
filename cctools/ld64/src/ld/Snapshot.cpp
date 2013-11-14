//
//  Snapshot.cpp
//  ld64
//
//  Created by Josh Behnke on 8/25/11.
//  Copyright (c) 2011 Apple Inc. All rights reserved.
//

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <libgen.h>
#include <time.h>
#include <Block.h>

#include "Snapshot.h"
#include "Options.h"

#include "compile_stubs.h"

//#define STORE_PID_IN_SNAPSHOT 1

// Well known snapshot file/directory names. These appear in the root of the snapshot.
// They are collected together here to make managing the namespace easier.
static const char *frameworksString         = "frameworks";         // directory containing framework stubs (mach-o files)
static const char *dylibsString             = "dylibs";             // directory containing dylib stubs (mach-o files)
static const char *archiveFilesString       = "archive_files";      // directory containing .a files
static const char *origCommandLineString    = "orig_command_line";  // text file containing the original command line
static const char *linkCommandString        = "link_command";       // text file containing the snapshot equivalent command line
static const char *dataFilesString          = "data_files";         // arbitrary data files referenced on the command line
static const char *objectsString            = "objects";            // directory containing object files
static const char *frameworkStubsString     = "framework_stubs";    // directory containing framework stub info (text files)
static const char *dylibStubsString         = "dylib_stubs";        // directory containing dylib stub info (text files)
static const char *assertFileString         = "assert_info";        // text file containing assertion failure logs
static const char *compileFileString        = "compile_stubs";      // text file containing compile_stubs script

Snapshot *Snapshot::globalSnapshot = NULL;

Snapshot::Snapshot() : fRecordArgs(false), fRecordObjects(false), fRecordDylibSymbols(false), fRecordArchiveFiles(false), fRecordUmbrellaFiles(false), fRecordDataFiles(false), fFrameworkArgAdded(false), fSnapshotLocation(NULL), fSnapshotName(NULL), fRootDir(NULL), fFilelistFile(-1), fCopiedArchives(NULL) 
{
    if (globalSnapshot != NULL)
        throw "only one snapshot supported";
    globalSnapshot = this;
}


Snapshot::~Snapshot() 
{
    // Lots of things leak under the assumption the linker is about to exit.
}


void Snapshot::setSnapshotPath(const char *path) 
{
    if (fRootDir == NULL) {
        fSnapshotLocation = strdup(path);
    }
}


void Snapshot::setSnapshotMode(SnapshotMode mode) 
{
    if (fRootDir == NULL) {
        fRecordArgs = false;
        fRecordObjects = false;
        fRecordDylibSymbols = false;
        fRecordArchiveFiles = false;
        fRecordUmbrellaFiles = false;
        fRecordDataFiles = false;
        
        switch (mode) {
            case SNAPSHOT_DISABLED:
                break;
            case SNAPSHOT_DEBUG:
                fRecordArgs = fRecordObjects = fRecordDylibSymbols = fRecordArchiveFiles = fRecordUmbrellaFiles = fRecordDataFiles = true;
                break;
            default:
                break;
        }
    }
}

void Snapshot::setSnapshotName(const char *path)
{
    if (fRootDir == NULL) {
        const char *base = basename((char *)path);
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf)-1, "%s-%4.4d-%2.2d-%2.2d-%2.2d%2.2d%2.2d.ld-snapshot", base, t.tm_year+1900, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        fSnapshotName = strdup(buf);
    }
}


// Construct a path string in the snapshot.
// subdir - an optional subdirectory name
// file - the file name
void Snapshot::buildPath(char *buf, const char *subdir, const char *file) 
{
    if (fRootDir == NULL)
        throw "snapshot not created";
    
    strcpy(buf, fRootDir);
    strcat(buf, "/");
    if (subdir) {
        strcat(buf, subdir);
        // implicitly create the subdirectory
        mkdir(buf, S_IRUSR|S_IWUSR|S_IXUSR);
        strcat(buf, "/");
    }
    if (file != NULL)
        strcat(buf, basename((char *)file));
}


// Construct a unique path string in the snapshot. If a path collision is detected then uniquing
// is accomplished by appending a counter to the path until there is no preexisting file.
// subdir - an optional subdirectory name
// file - the file name
void Snapshot::buildUniquePath(char *buf, const char *subdir, const char *file) 
{
    buildPath(buf, subdir, file);
    struct stat st;
    if (stat(buf, &st)==0) {
        // make it unique
        int counter=1;
        char *number = strrchr(buf, 0);
        number[0]='-';
        number++;
        do {
            sprintf(number, "%d", counter++);
        } while (stat(buf, &st) == 0);
    }
}


// Copy a file to the snapshot.
// sourcePath is the original file
// subdir is an optional subdirectory in the snapshot
// path is an optional out parameter containing the final uniqued path in the snapshot
// where the file was copied
void Snapshot::copyFileToSnapshot(const char *sourcePath, const char *subdir, char *path) 
{
    const int copyBufSize=(1<<14); // 16kb buffer
    static void *copyBuf = NULL;
    if (copyBuf == NULL)
        copyBuf = malloc(copyBufSize);
    
    char *file=basename((char *)sourcePath);
    char buf[PATH_MAX];
    if (path == NULL) path = buf;
    buildUniquePath(path, subdir, file);
    int out_fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    int in_fd = open(sourcePath, O_RDONLY);
    int len;
    if (out_fd != -1 && in_fd != -1) {
        do {
            len = read(in_fd, copyBuf, copyBufSize);
            if (len > 0) write(out_fd, copyBuf, len);
        } while (len == copyBufSize);
    }
    close(in_fd);
    close(out_fd);
}


// Create the snapshot root directory.
void Snapshot::createSnapshot()
{
    if (fRootDir == NULL) {
        // provide default name and location
        if (fSnapshotLocation == NULL)
            fSnapshotLocation = "/tmp";        
        if (fSnapshotName == NULL) {
            setSnapshotName("ld_snapshot");
        }
        
        char buf[PATH_MAX];
        fRootDir = (char *)fSnapshotLocation;
        buildUniquePath(buf, NULL, fSnapshotName);
        fRootDir = strdup(buf);
        if (mkdir(fRootDir, S_IRUSR|S_IWUSR|S_IXUSR)!=0) {
            warning("unable to create link snapshot directory: %s", fRootDir);
            fRootDir = NULL;
            setSnapshotMode(SNAPSHOT_DISABLED); // don't try to write anything if we can't create snapshot dir
        }
        
        buildPath(buf, NULL, compileFileString);
        int compileScript = open(buf, O_WRONLY|O_CREAT|O_TRUNC, S_IXUSR|S_IRUSR|S_IWUSR);
        write(compileScript, compile_stubs, strlen(compile_stubs));
        close(compileScript);

        SnapshotLog::iterator it;
        for (it = fLog.begin(); it != fLog.end(); it++) {
            void (^logItem)(void) = *it;
            logItem();
            Block_release(logItem);
        }
        fLog.erase(fLog.begin(), fLog.end());
        
        if (fRecordArgs) {
            writeCommandLine(fRawArgs, origCommandLineString, true);
            writeCommandLine(fArgs);
        }
        
#if STORE_PID_IN_SNAPSHOT
        char path[PATH_MAX];
        buildUniquePath(path, NULL, pidString);
        int pidfile = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
        char pid_buf[32];
        sprintf(pid_buf, "%lu\n", (long unsigned)getpid());
        write(pidfile, pid_buf, strlen(pid_buf));
        write(pidfile, "\n", 1);
        close(pidfile);    
#endif
        
    }
}


// Write the current command line vector to filename.
void Snapshot::writeCommandLine(StringVector &args, const char *filename, bool includeCWD) 
{
    if (!isLazy() && fRecordArgs) {
        // Figure out the file name and open it.
        if (filename == NULL)
            filename = linkCommandString;
        char path[PATH_MAX];
        buildPath(path, NULL, filename);
        int argsFile = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IXUSR|S_IRUSR|S_IWUSR);
        FILE *argsStream = fdopen(argsFile, "w");
        
        if (includeCWD)
            fprintf(argsStream, "cd %s\n", getcwd(path, sizeof(path)));

        // iterate to write args, quoting as needed
        StringVector::iterator it;
        for (it = args.begin(); it != args.end(); it++) {
            const char *arg = *it;
            bool needQuotes = false;
            for (const char *c = arg; *c != 0 && !needQuotes; c++) {
                if (isspace(*c))
                    needQuotes = true;
            }
            if (it != args.begin()) fprintf(argsStream, " ");
            if (needQuotes) fprintf(argsStream, "\"");
            fprintf(argsStream, "%s", arg);
            if (needQuotes) fprintf(argsStream, "\"");
        }
        fprintf(argsStream, "\n");
        fclose(argsStream);
    }
}


// Store the command line args in the snapshot.
void Snapshot::recordRawArgs(int argc, const char *argv[])
{
    // first store the original command line as-is
    for (int i=0; i<argc; i++) {
        fRawArgs.push_back(argv[i]);
    }
    fArgs.insert(fArgs.begin(), argv[0]);
    fArgs.insert(fArgs.begin()+1, "-Z"); // don't search standard paths when running in the snapshot
}


// Adds one or more args to the snapshot link command.
// argIndex is the index in the original raw args vector to start adding args
// argCount is the count of args to copy from the raw args vector
// fileArg is the index relative to argIndex of a file arg. The file is copied into the
// snapshot and the path is fixed up in the snapshot link command. (skipped if fileArg==-1)
void Snapshot::addSnapshotLinkArg(int argIndex, int argCount, int fileArg)
{
    if (fRootDir == NULL) {
        fLog.push_back(Block_copy(^{ this->addSnapshotLinkArg(argIndex, argCount, fileArg); }));
    } else {
        char buf[PATH_MAX];
        const char *subdir = dataFilesString;
        for (int i=0, arg=argIndex; i<argCount && argIndex+1<(int)fRawArgs.size(); i++, arg++) {
            if (i != fileArg) {
                fArgs.push_back(fRawArgs[arg]);
            } else {
                if (fRecordDataFiles) {
                    copyFileToSnapshot(fRawArgs[arg], subdir, buf);
                    fArgs.push_back(strdup(snapshotRelativePath(buf)));
                } else {
                    // if we don't copy the file then just record the original path
                    fArgs.push_back(strdup(fRawArgs[arg]));
                }
            }
        }
    }
}

// Record the -arch string
void Snapshot::recordArch(const char *arch)
{
    // must be called after recordRawArgs()
    if (fRawArgs.size() == 0)
        throw "raw args not set";

    // only need to store the arch explicitly if it is not mentioned on the command line
    bool archInArgs = false;
    StringVector::iterator it;
    for (it = fRawArgs.begin(); it != fRawArgs.end() && !archInArgs; it++) {
        const char *arg = *it;
        if (strcmp(arg, "-arch") == 0)
            archInArgs = true;
    }
    
    if (!archInArgs) {
        if (fRootDir == NULL) {
            fLog.push_back(Block_copy(^{ this->recordArch(arch); }));
        } else {
            char path_buf[PATH_MAX];
            buildUniquePath(path_buf, NULL, "arch");
            int fd=open(path_buf, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
            write(fd, arch, strlen(arch));
            close(fd);
        }
    }
}

// Record an object file in the snapshot.
// path - the object file's path
// fileContent - a pointer to the object file content
// fileLength - the buffer size of fileContent
void Snapshot::recordObjectFile(const char *path) 
{
    if (fRootDir == NULL) {
        fLog.push_back(Block_copy(^{ this->recordObjectFile(path); }));
    } else {
        if (fRecordObjects) {
			char path_buf[PATH_MAX];
			copyFileToSnapshot(path, objectsString, path_buf);
            
            // lazily open the filelist file
            if (fFilelistFile == -1) {
                char filelist_path[PATH_MAX];
                buildUniquePath(filelist_path, objectsString, "filelist");
                fFilelistFile = open(filelist_path, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
                fArgs.push_back("-filelist");
                fArgs.push_back(strdup(snapshotRelativePath(filelist_path)));
                writeCommandLine(fArgs);
            }
            
            // record the snapshot path in the filelist
            const char *relative_path = snapshotRelativePath(path_buf);
            write(fFilelistFile, relative_path, strlen(relative_path));
            write(fFilelistFile, "\n", 1);
        }
    }
}

void Snapshot::addFrameworkArg(const char *framework)
{
    bool found=false;
    for (unsigned i=0; i<fArgs.size()-1; i++) {
        if (strcmp(fArgs[i], "-framework") == 0 && strcmp(fArgs[i+1], framework) == 0)
            found = true;
    }
    if (!found) {
        if (!fFrameworkArgAdded) {
            fFrameworkArgAdded = true;
            fArgs.push_back("-Fframeworks");
        }
        fArgs.push_back("-framework");
        fArgs.push_back(strdup(framework));
        writeCommandLine(fArgs);
    }
}

void Snapshot::addDylibArg(const char *dylib)
{
    bool found=false;
    for (unsigned i=0; i<fArgs.size()-1; i++) {
        if (strcmp(fArgs[i], dylib) == 0)
            found = true;
    }
    if (!found) {
        char buf[ARG_MAX];
        sprintf(buf, "%s/%s", dylibsString, dylib);
        fArgs.push_back(strdup(buf));
        writeCommandLine(fArgs);
    }
}

// Record a dylib symbol reference in the snapshot.
// (References are not written to the snapshot until writeStubDylibs() is called.)
void Snapshot::recordDylibSymbol(ld::dylib::File* dylibFile, const char *name)
{
    if (fRootDir == NULL) {
        fLog.push_back(Block_copy(^{ this->recordDylibSymbol(dylibFile, name); }));
    } else {
        if (fRecordDylibSymbols) {
            // find the dylib in the table
            DylibMap::iterator it;
            const char *dylibPath = dylibFile->path();
            it = fDylibSymbols.find(dylibPath);
            bool isFramework = (strstr(dylibPath, "framework") != NULL);
            int dylibFd;
            if (it == fDylibSymbols.end()) {
                // Didn't find a file descriptor for this dylib. Create one and add it to the dylib map.
                char path_buf[PATH_MAX];
                buildUniquePath(path_buf, isFramework ? frameworkStubsString : dylibStubsString, dylibPath);
                dylibFd = open(path_buf, O_WRONLY|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR);
                fDylibSymbols.insert(std::pair<const char *, int>(dylibPath, dylibFd));
                char *base_name = strdup(basename(path_buf));
                if (isFramework) {
                    addFrameworkArg(base_name);
                } else {
                    addDylibArg(base_name);
                }
                writeCommandLine(fArgs);
            } else {
                dylibFd = it->second;
            }
            // Record the symbol.
            
            bool isIdentifier = (name[0] == '_');
            for (const char *c = name; *c != 0 && isIdentifier; c++)
                if (!isalnum(*c) && *c!='_')
                    isIdentifier = false;
            const char *prefix = "void ";
            const char *weakAttr = "__attribute__ ((weak)) ";
            const char *suffix = "(void){}\n";
            if (isIdentifier) {
                write(dylibFd, prefix, strlen(prefix));
                if (dylibFile->hasWeakExternals() && dylibFile->hasWeakDefinition(name))
                    write(dylibFd, weakAttr, strlen(weakAttr));
                if (*name == '_') name++;
                write(dylibFd, name, strlen(name));
                write(dylibFd, suffix, strlen(suffix));
            } else {
                static int symbolCounter = 0;
                char buf[64+strlen(name)];
                sprintf(buf, "void s_%5.5d(void) __asm(\"%s\");\nvoid s_%5.5d(){}\n", symbolCounter, name, symbolCounter);
                write(dylibFd, buf, strlen(buf));
                symbolCounter++;
            }
        }                
    }
}


// Record a .a archive in the snapshot.
void Snapshot::recordArchive(const char *archiveFile)
{
    if (fRootDir == NULL) {
        const char *copy = strdup(archiveFile);
        fLog.push_back(Block_copy(^{ this->recordArchive(archiveFile); ::free((void *)copy); }));
    } else {
        if (fRecordArchiveFiles) {
            // lazily create a vector of .a files that have been added
            if (fCopiedArchives == NULL) {
                fCopiedArchives = new StringVector;
            }
            
            // See if we have already added this .a
            StringVector::iterator it;
            bool found = false;
            for (it = fCopiedArchives->begin(); it != fCopiedArchives->end() && !found; it++) {
                if (strcmp(archiveFile, *it) == 0)
                    found = true;
            }
            
            // If this is a new .a then copy it to the snapshot and add it to the snapshot link command.
            if (!found) {
                char path[PATH_MAX];
                fCopiedArchives->push_back(archiveFile);
                copyFileToSnapshot(archiveFile, archiveFilesString, path);
                fArgs.push_back(strdup(snapshotRelativePath(path)));
                writeCommandLine(fArgs);
            }
        }
    }
}

void Snapshot::recordSubUmbrella(const char *frameworkPath)
{
    if (fRootDir == NULL) {
        const char *copy = strdup(frameworkPath);
        fLog.push_back(Block_copy(^{ this->recordSubUmbrella(copy); ::free((void *)copy); }));
    } else {
        if (fRecordUmbrellaFiles) {
            const char *framework = basename((char *)frameworkPath);
            char buf[PATH_MAX], wrapper[PATH_MAX];
            strcpy(wrapper, frameworksString);
            buildPath(buf, wrapper, NULL); // ensure the frameworks directory exists
            strcat(wrapper, "/");
            strcat(wrapper, framework);
            strcat(wrapper, ".framework");
            copyFileToSnapshot(frameworkPath, wrapper);
            addFrameworkArg(framework);
        }
    }
}

void Snapshot::recordSubLibrary(const char *dylibPath)
{
    if (fRootDir == NULL) {
        const char *copy = strdup(dylibPath);
        fLog.push_back(Block_copy(^{ this->recordSubLibrary(copy); ::free((void *)copy); }));
    } else {
        if (fRecordUmbrellaFiles) {
            copyFileToSnapshot(dylibPath, dylibsString);
            addDylibArg(basename((char *)dylibPath));
        }
    }
}

void Snapshot::recordAssertionMessage(const char *fmt, ...)
{
    char *msg;
    va_list args;
    va_start(args, fmt);
    vasprintf(&msg, fmt, args);
    va_end(args);
    if (msg != NULL) {
        if (fRootDir == NULL) {
            fLog.push_back(Block_copy(^{ this->recordAssertionMessage("%s", msg); free(msg); }));
        } else {
            char path[PATH_MAX];
            buildPath(path, NULL, assertFileString);
            int log = open(path, O_WRONLY|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR);
            write(log, msg, strlen(msg));
            close(log);
            free(msg);
        }    
    }
}
