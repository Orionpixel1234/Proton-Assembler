DEFAULT REL

SECTION .data
filename DB "test.asm",0

SECTION .bss
buffer RESB 1024

SECTION .text
GLOBAL _start

_start:
    ;; ------------------------
    ;; OPEN(FILENAME, O_RDONLY)
    ;; ------------------------
    MOV RAX, 2                ;; SYS_OPEN
    LEA RDI, [rel filename]   ;; FILE TO OPEN (RIP-relative)
    XOR RSI, RSI              ;; FLAGS = O_RDONLY (0)
    XOR RDX, RDX              ;; MODE = (not used)
    SYSCALL
    MOV R12, RAX              ;; FILE DESCRIPTOR

    CMP R12, 0                ;; CHECK IF FILE OPENED
    JS exit_error             ;; NEGATIVE = ERROR

read_loop:
    ;; ------------------------
    ;; READ(FD, BUFFER, 1024)
    ;; ------------------------
    MOV RAX, 0               ;; SYS_READ
    MOV RDI, R12             ;; FILE DESCRIPTOR
    LEA RSI, [rel buffer]    ;; BUFFER (RIP-relative)
    MOV RDX, 1024            ;; BYTES TO READ
    SYSCALL

    MOV R8, RAX              ;; R8 = BYTES READ
    LEA R9, [rel buffer]     ;; R9 = BUFFER START

    CMP R8, 0                ;; EOF?
    JE done                  ;; IF ZERO, WE'RE DONE

scan_loop:
    CMP R8, 3               ;; CHECK IF WE HAVE AT LEAST 3 BYTES TO PROCESS
    JL write_char           ;; IF LESS THAN 3 BYTES, WRITE THEM AS NORMAL

    CMP WORD [R9], 'HL'     ;; CHECK IF FIRST 3 BYTES ARE 'HLT' (0x5448 in LITTLE ENDIAN)
    JNE write_char     ;; IF NOT 'HLT', CONTINUE NORMAL WRITE LOOP
    CMP BYTE [R9+2], 'T'  ;; CHECK THIRD BYTE IS 'T'
    JNE write_char     ;; IF NOT 'HLT', CONTINUE NORMAL WRITE LOOP
    MOV RDI, R9           ;; BUFFER POINTER
    CALL handle_hlt

write_char:
    ;; ------------------------
    ;; WRITE(1, BUFFER, RAX)
    ;; ------------------------
    MOV RAX, 1                ;; SYS_WRITE
    MOV RDI, 1                ;; STDOUT
    MOV RSI, R9               ;; BUFFER
    MOV RDX, 1                ;; BYTES TO WRITE
    SYSCALL

    INC R9
    DEC R8

    CMP R8, 0
    JG scan_loop

    JMP read_loop

handle_hlt:
    MOV RAX, 1               ;; SYS_WRITE
    MOV RDI, 1               ;; STDOUT
    MOV RSI, R9              ;; BUFFER
    MOV RDX, 3               ;; BYTES TO WRITE ('HLT')
    ADD R9, 3                ;; ADVANCE BUFFER POINTER PAST 'HLT'
    SUB R8, 3                ;; DECREASE BYTES TO WRITE BY 3
    
    SYSCALL
    RET

done:
    ;; ------------------------
    ;; CLOSE(FD)
    ;; ------------------------
    MOV RAX, 3               ;; SYS_CLOSE
    MOV RDI, R12             ;; FD
    SYSCALL

    ;; ------------------------
    ;; EXIT(CODE)
    ;; ------------------------
    MOV RAX, 60              ;; SYS_EXIT
    XOR RDI, RDI             ;; EXIT CODE 0
    SYSCALL

exit_error:
    MOV RAX, 60              ;; SYS_EXIT
    MOV RDI, 1               ;; EXIT CODE 1 (ERROR)
    SYSCALL
