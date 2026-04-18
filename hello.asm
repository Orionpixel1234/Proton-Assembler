[BITS 16]
[ORG 0x7C00]

main:
    CLI
    XOR AX, AX
    MOV DS, AX
    MOV ES, AX
    MOV SS, AX
    MOV SP, 0x7A00
    STI

    MOV SI, hello_msg
    CALL print_line     ; defined in print.asm

halt:
    HLT
    JMP halt

hello_msg DB "Hello, World!", 13, 10, 0

[INCLUDE "print.asm"]

TIMES 510-($-$$) DB 0
DW 0xAA55