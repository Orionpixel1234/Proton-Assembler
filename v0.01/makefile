$(V001_O): $(V001_SRC) | $(BIN)
	$(AS) $(ASFLAGS) $(V001_SRC) -o $(V001_O)

$(V001): $(V001_O) | $(BIN)
	$(LD) $(V001_O) -o $(V001)