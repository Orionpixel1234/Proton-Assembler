[BITS 32]
GLOBAL _start

_start:
    MOV EAX, 1          ; syscall: sys_exit
    XOR EBX, eBx        ; status: 0
    MOV RAX, 0xDEADBEEF   ; dummy value to test 64-bit register encoding
    MOV RBP, RSP
    int 0x80            ; call kernel