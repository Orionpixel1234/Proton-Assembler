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

    CMP RAX, 0               ;; EOF?
    JE done                  ;; IF ZERO, WE'RE DONE

    MOV R8, RAX           ;; R8 = BYTES READ
    LEA R9, [rel buffer]  ;; R9 = BUFFER START

    MOV RCX, [rel buffer] ;; READ FIRST 8 BYTES INTO RCX FOR COMPARISON
    CMP WORD CX, 'HLT'    ;; CHECK IF FIRST 3 BYTES ARE 'HLT' (0x5448 in LITTLE ENDIAN)
    JNE continue_loop     ;; IF NOT 'HLT', CONTINUE NORMAL WRITE LOOP
    MOV RDI, R9           ;; BUFFER POINTER
    CALL handle_hlt


continue_loop:
    ;; ------------------------
    ;; WRITE(1, BUFFER, RAX)
    ;; ------------------------
    MOV RDI, 1               ;; STDOUT
    MOV RSI, R9              ;; BUFFER
    MOV RDX, R8              ;; BYTES TO WRITE (RETURNED BY READ)
    MOV RAX, 1               ;; SYS_WRITE
    SYSCALL

    JMP read_loop

handle_hlt:
    MOV RAX, 1               ;; SYS_WRITE
    MOV RDI, 1               ;; STDOUT
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
