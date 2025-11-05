#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#ifndef __APPLE__
static const char *get_target_triple(const char *argv0) {
    const char *env = getenv("CCTOOLS_CLANG_AS_TARGET_TRIPLE");
    if (env) {
        return env;
    }

    const char *progname = strrchr(argv0, '/');
    progname = progname ? progname + 1 : argv0;

    // Example: "i386-apple-darwin-as"
    static char triple_buf[64];
    const char *dash = strchr(progname, '-');
    if (dash) {
        size_t len = dash - progname;
        if (len < sizeof(triple_buf)) {
            snprintf(triple_buf, sizeof(triple_buf), "%.*s-apple-darwin", (int)len, progname);
            return triple_buf;
        }
    }

    return "x86_64-apple-darwin";
}
#endif

int main(int argc, char **argv) {
    /*
     * as has been replaced by a simple shell wrapper in the latest cctools sources:
     * https://raw.githubusercontent.com/apple-oss-distributions/cctools/3406a8e0f9ec28862967217797fe2b9a7b3d10ed/misc/as
     * 
     * We try to replicate the original as driver as much as possible:
     * https://github.com/tpoechtrager/cctools-port/blob/93ffa47ee2139aba177deb07de9b6626486037ae/cctools/as/driver.c#L300
     * 
     * clang is used for assembling.
    */

    bool some_input_files = false;
    bool oflag_specified = false;

    char *clang_exe = getenv("CCTOOLS_CLANG_AS_EXECUTABLE");
    clang_exe = clang_exe ? clang_exe : "clang";

    char **given_args = malloc(sizeof(char*) * argc);
    char **args = malloc(sizeof(char*) * (argc + 16));
    if (!given_args || !args) return 1;

    int given_i = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "-Q") == 0 || strcmp(arg, "-q") == 0) {
            continue;
        }

        /*
        * If we have not seen some some_input_files or a "-" or "--" to
        * indicate we are assembling stdin add a "-" so clang will
        * assemble stdin as as(1) would.
        */
        if (strcmp(arg, "--") == 0 || strcmp(arg, "-") == 0 || arg[0] != '-') {
            some_input_files = true;
        }

        // Track if -o is specified
        if (strcmp(arg, "-o") == 0 && i + 1 < argc) {
            oflag_specified = true;
        }

        given_args[given_i++] = argv[i];
    }

    int j = 0;
    args[j++] = clang_exe;

    /*
    * Add "-x assembler" in case the input does not end in .s this must
    * come before "-" or the clang driver will issue an error:
    * "error: -E or -x required when input is from standard input"
    */
    args[j++] = "-x";
    args[j++] = "assembler";

    /*
    * If we have not seen some some_input_files or a "-" or "--" to
    * indicate we are assembling stdin add a "-" so clang will
    * assemble stdin as as(1) would.
    */
    if (!some_input_files) {
        args[j++] = "-";
    }

    for (int i = 0; i < given_i; ++i) {
        /*
        * Translate as(1) use of "--" for stdin to clang's use of "-".
        */
        if (strcmp(given_args[i], "--") == 0) {
            args[j++] = "-";
        }
        /*
        * Do not pass command line argument that are Unknown to
        * to clang.
        */
        else if (strcmp(given_args[i], "-V") != 0) {
            args[j++] = given_args[i];
        }
    }

    /*
    * clang requires a "-o a.out" if not -o is specified.
    */
    if (!oflag_specified) {
        args[j++] = "-o";
        args[j++] = "a.out";
    }

    /* Add -integrated-as or clang will run as(1). */
    args[j++] = "-integrated-as";

    /* Add -c or clang will run ld(1). */
    args[j++] = "-c";

    /* Silence clang warnings for unused -I etc. */
    args[j++] = "-Wno-unused-command-line-argument";

#ifndef __APPLE__
    args[j++] = "-target";
    args[j++] = (char*)get_target_triple(argv[0]);
#endif

    args[j] = NULL;

    execvp(args[0], args);
    perror("execvp clang failed");
    return 1;
}
