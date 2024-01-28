#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // Prepare the arguments array for execvp
    char *new_argv[argc + 1]; // +1 for the NULL terminator
    new_argv[0] = "otool";
    for (int i = 1; i < argc; ++i) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL; // NULL-terminate the array

    // Replace the current process with otool
    execvp("otool", new_argv);
    // execvp only returns if an error occurred, try again with llvm-otool
    execvp("llvm-otool", new_argv);

    // Neither otool nor llvm-otool could be executed, print an error message and exit
    fprintf(stderr, "Could not execute otool or llvm-otool; llvm-otool comes with llvm 13 onwards\n");
    return EXIT_FAILURE;
}
