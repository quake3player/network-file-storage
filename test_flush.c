#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    char line[100];
    printf("Enter 'ETIRW' followed by more text:\n");
    if (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, "ETIRW") == 0) {
            printf("Matched ETIRW. Flushing stdin...\n");
            fflush(stdin);
            printf("Stdin flushed. Enter next line:\n");
            if (fgets(line, sizeof(line), stdin)) {
                printf("Next line: %s\n", line);
            } else {
                printf("EOF or error\n");
            }
        } else {
            printf("Did not match ETIRW: %s\n", line);
        }
    }
    return 0;
}
