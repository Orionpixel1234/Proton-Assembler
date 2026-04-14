;; pm.asm -- 32-bit protected mode entry point
;; Assembled with nasm (proton does not support [BITS 32]).
;; This code executes AFTER the far jump from boot2 has switched
;; the CPU into 32-bit protected mode.

[BITS 32]

SECTION .text
GLOBAL PM
EXTERN boot_main

PM:
    MOV EAX, 0x10       ;; DATA_SEG = gdt_data - gdt = 0x10
    MOV DS, EAX
    MOV SS, EAX
    MOV ESP, 0x90000    ;; 32-bit flat stack

    CALL boot_main

.halt:
    CLI
    HLT
    JMP .halt
