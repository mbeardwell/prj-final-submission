#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    for (int i = 0; i < atoi(argv[1]); i++) {
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
        asm volatile ("vadd.f32 S0, S0, S1");
    }
    return 0;
}
