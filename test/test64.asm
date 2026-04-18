[BITS 64]
[DEFAULT REL]

GLOBAL main
EXTERN printf

SECTION .rodata
.Lstr0:
    db "Hello, world!", 10, 0

SECTION .text

main:
    SUB RSP, 8
    MOV RDI, [RSP]
    LEA RAX, [.Lstr0]
    MOV RDI, RAX
    XOR EAX, EAX
    CALL printf
    ADD RSP, 8
    XOR EAX, EAX
    RET
