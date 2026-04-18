; print.asm — print_line function
; Prints a null-terminated string pointed to by SI
; Uses BIOS INT 0x10, AH=0x0E (teletype output)
; Clobbers: AL, AH, BH

print_line:
    LODSB               ; AL = [SI], SI++
    TEST AL, AL         ; null terminator?
    JZ .done
    MOV AH, 0x0E        ; BIOS teletype function
    MOV BH, 0           ; display page 0
    INT 0x10            ; print character in AL
    JMP print_line
.done:
    RET