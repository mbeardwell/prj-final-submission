PROJ_ROOT := $(shell pwd)/..
EXE_NAME := arm-fp-emu.so
EXE_PATH := $(PROJ_ROOT)/build/$(EXE_NAME)
LIBRUNT_DIR := $(PROJ_ROOT)/contrib/librunt
KEYSTONE_DIR := $(PROJ_ROOT)/contrib/keystone
PATCHES := $(PROJ_ROOT)/patches

TEST_BIN := $(PROJ_ROOT)/tests/build/vadd10

CFLAGS += -I$(LIBRUNT_DIR)/include
CFLAGS += -I$(KEYSTONE_DIR)/include
CFLAGS += -I/usr/include/capstone
CFLAGS += -g
CFLAGS += -fPIC
CFLAGS += -shared

LDFLAGS += -Wl,--defsym,__wrap___runt_files_metadata_by_addr=__runt_files_metadata_by_addr
LDFLAGS += -Wl,--defsym,__wrap___runt_files_notify_load=__runt_files_notify_load
LDFLAGS += -lstdc++ -lm -L$(KEYSTONE_DIR)/build/llvm/lib/ -lkeystone
LDFLAGS += -lcapstone

LIBS += $(LIBRUNT_DIR)/lib/librunt_preload.a

KS_OPTS += -DCMAKE_BUILD_TYPE=Release
KS_OPTS += -DBUILD_SHARED_LIBS=ON
KS_OPTS += -DLLVM_TARGETS_TO_BUILD=ARM
KS_OPTS += -DLLVM_BUILD_TESTS=OFF
KS_OPTS += -Wno-dev

.PHONY: default
default: 
	echo "Please read 'README.md' and follow the instructions."
	echo "The build process can take a very long time."

arm-fp-emu.so:
	$(CC) -w $(CFLAGS) arm-fp-emu.c $(LDFLAGS) $(LIBS) -o $(EXE_PATH)

.PHONY: build-librunt
build-librunt:
	# Patch librunt to support armel
	-patch --forward $(PROJ_ROOT)/contrib/librunt/src/preload.c $(PATCHES)/librunt-armel-src-preload.patch
	-patch --forward $(PROJ_ROOT)/contrib/librunt/src/librunt.c $(PATCHES)/librunt-armel-src-librunt.patch
	make -C $(PROJ_ROOT)/contrib/librunt

.PHONY: build-keystone
build-keystone:
	-! [ -d $(KEYSTONE_DIR)/build ] && mkdir $(KEYSTONE_DIR)/build
	cd $(KEYSTONE_DIR)/build && cmake $(KS_OPTS) -G "Unix Makefiles" ..
	cd $(KEYSTONE_DIR)/build && ../make-share.sh debug

.PHONY: clean
clean:
	rm -f $(EXE_PATH)
	-make -C $(PROJ_ROOT)/contrib/librunt/src clean
	-make -C $(PROJ_ROOT)/contrib/librunt/lib clean
	-make -C $(PROJ_ROOT)/contrib/librunt/test clean
	rm -rf $(KEYSTONE_DIR)/build/

.PHONY: build-tests
build-tests: 
	cd ../tests && make

.PHONY: test
test:
	LD_LIBRARY_PATH=$(KEYSTONE_DIR)/build/llvm/lib LD_PRELOAD=$(EXE_PATH) $(TEST_BIN) 10

GDB_FLAGS += -ex "set confirm off"
GDB_FLAGS += -ex "layout next"
GDB_FLAGS += -ex "file $(EXE_PATH)"
GDB_FLAGS += -ex "file $(TEST_BIN)"
GDB_FLAGS += -ex "set exec-wrapper env LD_LIBRARY_PATH=$(KEYSTONE_DIR)/build/llvm/lib LD_PRELOAD=$(EXE_PATH)"

.PHONY: debug
debug:
	# The test program must be ran with one integer argument.
	# Type 'run 10' to run for 10 iterations.
	gdb $(GDB_FLAGS)
