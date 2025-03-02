#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
    for (int i = 0; i < atoi(argv[1]); i++) {
        getpid();
    }
    return 0;
}
