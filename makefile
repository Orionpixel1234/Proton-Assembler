BUILD := build
BIN   := $(BUILD)/bin

CC     := gcc
CFLAGS := -m64 -fno-pie -no-pie

PROTON_SRC := proton.c
PROTON_O   := $(BIN)/proton.o
PROTON     := $(BUILD)/proton

# ── Default goal (must be first rule) ────────────────────────────────────────

.PHONY: all clean

all: $(BIN)/boot.img $(BIN)/hello.img

# ── Proton assembler binary ───────────────────────────────────────────────────

$(PROTON_O): $(PROTON_SRC) | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

$(PROTON): $(PROTON_O)
	$(CC) $(CFLAGS) $< -o $@

# ── Individual assembly targets ───────────────────────────────────────────────

$(BIN)/boot.bin: test/boot.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin ../test/boot.asm -o bin/boot.bin

$(BIN)/boot2.bin: test/boot2.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin ../test/boot2.asm -o bin/boot2.bin

$(BIN)/a20.bin: test/a20.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin ../test/a20.asm -o bin/a20.bin

$(BIN)/gdt.bin: test/gdt.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin ../test/gdt.asm -o bin/gdt.bin

$(BIN)/pm.bin: test/pm.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin ../test/pm.asm -o bin/pm.bin

$(BIN)/hello.bin: test/hello.asm test/print.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin ../test/hello.asm -o bin/hello.bin

# ── Disk images ───────────────────────────────────────────────────────────────

# boot.img: stage1 MBR (512 B) followed by stage2 (boot2 → a20 → gdt → pm)
$(BIN)/boot.img: $(BIN)/boot.bin $(BIN)/boot2.bin $(BIN)/a20.bin \
                 $(BIN)/gdt.bin  $(BIN)/pm.bin
	cat $^ > $@

# hello.img: standalone hello-world boot sector
$(BIN)/hello.img: $(BIN)/hello.bin
	cp $< $@

# ── Directory rules ───────────────────────────────────────────────────────────

$(BUILD):
	mkdir -p $@

$(BIN): | $(BUILD)
	mkdir -p $@

# ── Clean ─────────────────────────────────────────────────────────────────────

clean:
	rm -f $(PROTON_O) $(PROTON) \
	      $(BIN)/boot.bin  $(BIN)/boot2.bin $(BIN)/a20.bin \
	      $(BIN)/gdt.bin   $(BIN)/pm.bin    $(BIN)/hello.bin \
	      $(BIN)/boot.img  $(BIN)/hello.img
