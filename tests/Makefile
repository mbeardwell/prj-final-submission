ARCH_FLAGS += -mfloat-abi=softfp
ARCH_FLAGS += -mfpu=vfpv3-d16
ARCH_FLAGS += -march=armv7-a+fp
ARCH_FLAGS += -marm

CFLAGS += -O0 -g

.PHONY: default
default:
	gcc $(ARCH_FLAGS) $(CFLAGS) vadd.c -o ./build/vadd
	gcc $(ARCH_FLAGS) $(CFLAGS) vadd10.c -o ./build/vadd10
	gcc $(ARCH_FLAGS) $(CFLAGS) vadd100.c -o ./build/vadd100
	gcc $(ARCH_FLAGS) $(CFLAGS) vadd1000.c -o ./build/vadd1000
	gcc $(ARCH_FLAGS) $(CFLAGS) getpid.c -o ./build/getpid
