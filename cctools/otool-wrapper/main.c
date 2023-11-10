#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // Assuming llvm-otool binary is in the PATH, otherwise provide the full path
    char *binaryPath = "llvm-otool";

    // Prepare the arguments array for execvp
    char *new_argv[argc + 1]; // +1 for the NULL terminator
    new_argv[0] = binaryPath;
    for (int i = 1; i < argc; ++i) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL; // NULL-terminate the array

    // Replace the current process with llvm-otool binary
    execvp(binaryPath, new_argv);

    // execvp only returns if an error occurred
    fprintf(stderr, "Could not execute llvm-otool; llvm-otool comes with llvm 13 onwards\n");
    return EXIT_FAILURE;
}
