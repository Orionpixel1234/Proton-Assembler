BUILD := build
BIN := $(BUILD)/bin

AS := nasm
LD := ld
ASFLAGS := -f elf64

V001_SRC := v0.01/v001.asm
V001_O := $(BIN)/v001.o
V001 := $(BIN)/v001

include v0.01/makefile

$(BUILD):
	mkdir -p $@

$(BIN): | $(BUILD)
	mkdir -p $@

run: $(V001) | $(BIN)
	cd $(BIN) && ./v001

include build/bin/makefile

all: test

