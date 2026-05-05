#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// print one status line per second so the scheduler can show visible progress.
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s N\n", argv[0]);
        return 1;
    }

    char *endptr = NULL;
    long n = strtol(argv[1], &endptr, 10);
    if (endptr == argv[1] || *endptr != '\0' || n < 0) {
        fprintf(stderr, "Usage: %s N\n", argv[0]);
        return 1;
    }

    // start at zero so the output matches the screenshots that show 0/N style progress.
    for (long i = 0; i <= n; i++) {
        printf("Demo %ld/%ld\n", i, n);
        fflush(stdout);

        // the last line prints immediately after the final second, so the program still behaves like a timed workload.
        if (i < n) {
            sleep(1);
        }
    }

    return 0;
}
