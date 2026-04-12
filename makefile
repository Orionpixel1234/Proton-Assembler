BUILD := build
BIN := $(BUILD)/bin

CC := gcc
CFLAGS := -m64 -fno-pie -no-pie
LD := ld
LDFLAGS := -m elf_x86_64 -Ttext 0x400000 --oformat elf64-x86-64
AS := nasm
ASFLAGS := -f elf64

V001_SRC := v0.01/v001.c
V001_O := $(BUILD)/v001.o
V001 := $(BIN)/v001

include v0.01/makefile

$(BUILD):
	mkdir -p $@

$(BIN): | $(BUILD)
	mkdir -p $@

run: $(V001) | $(BIN)
	cd $(BIN) && ./v001 -f bin test.asm -o test.bin

all: run

