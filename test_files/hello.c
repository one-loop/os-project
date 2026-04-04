#include <stdio.h>

int main() {
    // simple helper program used to verify myshell can execute local binaries.
    printf("Hello from custom program!\n");
    // second line makes test output obvious when run through the shell.
    printf("This program was executed by myshell.\n");
    // return success to the parent process.
    return 0;
}
