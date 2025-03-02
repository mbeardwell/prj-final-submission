.PHONY: default
default: 
	echo "Please read 'README.md' and follow the instructions."
	echo "The build process can take a very long time."

.PHONY: librunt
librunt:
	make -C src build-librunt

.PHONY: keystone
keystone:
	make -C src build-keystone

.PHONY: arm-fp-emu
arm-fp-emu:
	make -C src arm-fp-emu.so

.PHONY: build-tests
build-tests:
	make -C src build-tests

.PHONY: clean
clean:
	make -C src clean

.PHONY: test
test:
	make -C src test

.PHONY: debug
debug:
	make -C src debug