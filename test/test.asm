[BITS 32]
GLOBAL _start

_start:
    MOV EAX, 1          ; syscall: sys_exit
    XOR EBX, eBx        ; status: 0
    int 0x80            ; call kernel