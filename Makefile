# SPDX-License-Identifier: GPL-3.0-or-later
# Use the BlocksDS default path unless the installation sets BLOCKSDS.
export BLOCKSDS ?= /opt/blocksds/core

.DEFAULT_GOAL := all
.PHONY: all patches test clean

all: patches
	$(MAKE) -C bootloader
	$(MAKE) -f Makefile.arm9

patches:
	python3 patches/build.py
	python3 patches/ntr_codec_table.py

test:
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		source/arguments.c source/config.c source/header.c \
		source/dldi_relocate.c source/tgds_patch.c tests/arguments_test.c \
		-o build/arguments_test
	./build/arguments_test
	$(CC) -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Iinclude \
		source/arguments.c source/config.c source/preferences.c \
		tests/menu_test.c -o build/menu_test
	./build/menu_test
	python3 tests/ui_integration.py

clean:
	$(MAKE) -C bootloader clean
	rm -rf build data/load.bin snemulds-shim.nds
