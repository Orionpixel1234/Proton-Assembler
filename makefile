BUILD := build
BIN   := $(BUILD)/bin

CC     := gcc
CFLAGS := -m64 -fno-pie -no-pie

PROTON_SRC := proton.c
PROTON_O   := $(BIN)/proton.o
PROTON     := $(BUILD)/proton

# ── Proton assembler binary ───────────────────────────────────────────────────

$(PROTON_O): $(PROTON_SRC) | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

$(PROTON): $(PROTON_O)
	$(CC) $(CFLAGS) $< -o $@

# ── Assembly targets ──────────────────────────────────────────────────────────

$(BIN)/test.bin: $(BIN)/test.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin bin/test.asm -o bin/test.bin

$(BIN)/hello.bin: $(BIN)/hello.asm $(BIN)/print.asm | $(PROTON)
	cd $(BUILD) && ./proton -f bin bin/hello.asm -o bin/hello.bin

# ── Directory rules ───────────────────────────────────────────────────────────

$(BUILD):
	mkdir -p $@

$(BIN): | $(BUILD)
	mkdir -p $@

# ── Phony targets ─────────────────────────────────────────────────────────────

.PHONY: all test hello clean

all: test hello

test: $(BIN)/test.bin

hello: $(BIN)/hello.bin

clean:
	rm -f $(PROTON_O) $(PROTON) $(BIN)/test.bin $(BIN)/hello.bin
