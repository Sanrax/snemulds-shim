# SPDX-License-Identifier: GPL-3.0-or-later
BLOCKSDS ?= /opt/blocksds/core
WONDERFUL_TOOLCHAIN ?= /opt/wonderful
PREFIX := $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
UP := ../third_party/nds-bootloader
BUILD ?= ../build/loader
OUTPUT ?= ../data/load.bin
HANDOFF ?= twl_handoff.c
CFLAGS := -std=gnu11 -Os -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
 -mcpu=arm7tdmi -mthumb -DARM7 -ffreestanding -fno-builtin \
 -fno-strict-aliasing -I$(BLOCKSDS)/libs/libnds/include -I$(UP)/source -I../include
ASFLAGS := -mcpu=arm7tdmi -x assembler-with-cpp -I$(BUILD)
OBJS := $(addprefix $(BUILD)/,boot.o sdmmc.o bios.o fat.o dldi_patcher.o load_crt0.o io_dldi.o arm7clear.o arm9mpu_reset.o arm9_stages.o support.o tgds_io.o twl_handoff.o ntr_handoff.o)
.DEFAULT_GOAL := all
.PHONY: all clean
all: $(OUTPUT)
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: $(UP)/source/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: $(UP)/source/%.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/support.o: support.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/twl_handoff.o: $(HANDOFF) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ntr_handoff.o: ntr_handoff.c ../include/ntr_codec_generated.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tgds_io.o: ../source/tgds_io.c ../include/tgds_io.h ../include/twl_input_generated.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/arm9_stages.o: arm9_stages.s | $(BUILD)
	$(CC) -mcpu=arm946e-s -c $< -o $@
$(BUILD)/mpu_reset.elf: $(UP)/arm9code/mpu_reset.s | $(BUILD)
	$(CC) -x assembler-with-cpp -I$(BLOCKSDS)/libs/libnds/include -mcpu=arm946e-s -nostdlib -Wl,-Ttext=0 -Wl,-e,_start $< -o $@
$(BUILD)/mpu_reset.bin: $(BUILD)/mpu_reset.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/arm9mpu_reset.o: $(BUILD)/mpu_reset.bin
$(BUILD)/loader.elf: $(OBJS)
	$(CC) -mcpu=arm7tdmi -mthumb -nostdlib -T$(UP)/load.ld \
	 -Wl,-Map,$(BUILD)/loader.map -Wl,--no-warn-rwx-segments $(OBJS) -lgcc -o $@
$(OUTPUT): $(BUILD)/loader.elf
	mkdir -p $(dir $(OUTPUT))
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf $(BUILD) ../data/load.bin
