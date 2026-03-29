#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define YSIZE 24
#define XSIZE 80

int main() {
    int i, j;
    char id[100];

    clock_t start = clock();

    // Run the loop many times to get a measurable time
    for (int iter = 0; iter < 100000; iter++) {
        for (i = 0; i < YSIZE + 2; i++) {
            size_t id_len;
            (void)snprintf(id, sizeof id, "%d: ", i);
            id_len = strlen(id);
            for (j = 0; j < XSIZE - id_len; j++) {
                // do nothing, just evaluate the condition
                volatile int x = '0' + (i % 10);
            }
        }
    }

    clock_t end = clock();
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time: %f\n", cpu_time_used);

    return 0;
}
