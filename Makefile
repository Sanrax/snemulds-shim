# SPDX-License-Identifier: GPL-3.0-or-later
# Use the BlocksDS default path unless the installation sets BLOCKSDS.
export BLOCKSDS ?= /opt/blocksds/core

.DEFAULT_GOAL := all
.PHONY: all patches clean

all: patches
	$(MAKE) -C bootloader
	$(MAKE) -f Makefile.arm9

patches:
	python3 patches/build.py
	python3 patches/ntr_codec_table.py

clean:
	$(MAKE) -C bootloader clean
	rm -rf build data/load.bin snemulds-shim.nds
