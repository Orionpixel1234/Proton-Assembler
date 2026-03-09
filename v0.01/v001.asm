DEFAULT REL

SECTION .data
filename    DB "test.asm", 0
o_filename  DB "test.bin", 0
rex_w       DB 0x48
mov_opcode  DB 0xB8
rax_opcode  DB 0x00
rcx_opcode  DB 0x01

SECTION .bss
buffer  resb 1024
opcode  resb 1
value   resq 1
digit   resb 1

SECTION .text
GLOBAL _start

_start:
    ;; --------------------------------
    ;; OPEN(RAX = 2, RDI = filename, RSI = flags, RDX = mode)

    MOV RAX, 2                ;; SYS_OPEN
    LEA RDI, [rel filename]   ;; FILE TO OPEN (RIP-relative)
    XOR RSI, RSI              ;; FLAGS = O_RDONLY (0)
    XOR RDX, RDX              ;; MODE = (not used)
    SYSCALL
    MOV R12, RAX
    CMP R12, 0
    JS exit_error
    ;; --------------------------------

    ;; --------------------------------
    ;; OPEN(RAX = 0, RDI = filename, RSI = flags, RDX = mode)
    MOV RAX, 2                  ;; SYS_OPEN
    LEA RDI, [rel o_filename]   ;; FILENAME
    MOV RSI, 557                ;; O_WRONLY | O_CREAT | O_TRUNC = 0x241
    MOV RDX, 0644               ;; FILE MODE (rw-r--r--)
    SYSCALL
    MOV R13, RAX                ;; FILE DESCRIPTOR FOR OUTPUT FILE
    CMP R13, 0
    JS exit_error
    ;; --------------------------------
read_loop:
    ;; --------------------------------
    ;; READ(RAX = 0, RDI = fd, RSI = buffer, RDX = count)
    MOV RAX, 0 ;; SYS_READ
    MOV RDI, R12 ;; FD
    LEA RSI, [rel buffer]
    MOV RDX, 1024 ;; COUNT
    SYSCALL

    TEST RAX, RAX
    JZ exit
    JS exit_error

    MOV R14, RAX ;; BYTES READ
    LEA R15, [rel buffer] ;; BUFFER POINTER
    ;; --------------------------------
scan_loop:
    CMP R14, 12             ;; Need at least 12 bytes for MOV R?, 0xIMM64
    JL read_loop

    CMP BYTE [R15], 'M'
    JNE skip
    CMP BYTE [R15+1], 'O'
    JNE skip
    CMP BYTE [R15+2], 'V'
    JNE skip
    CALL handle_mov
    JMP scan_loop

skip:
    INC R15
    DEC R14
    CMP R14, 12
    JGE scan_loop
    JMP read_loop

exit:
    ;; --------------------------------
    ;; CLOSE(RAX = 3, RDI = fd)
    MOV RAX, 3 ;; SYS_CLOSE
    MOV RDI, R12 ;; FD
    SYSCALL
    ;; --------------------------------

    ;; --------------------------------
    ;; CLOSE(RAX = 3, RDI = fd)
    MOV RAX, 3 ;; SYS_CLOSE
    MOV RDI, R13 ;; FD
    SYSCALL
    ;; --------------------------------

    ;; --------------------------------
    ;; EXIT(RAX = 60, RDI = status)
    MOV RAX, 60 ;; SYS_EXIT
    XOR RDI, RDI ;; STATUS = 0
    SYSCALL
    ;; --------------------------------
 
exit_error:
    ;; --------------------------------
    ;; EXIT(RAX = 60, RDI = status)
    MOV RAX, 60 ;; SYS_EXIT
    MOV RDI, 1 ;; STATUS = 0
    SYSCALL
    ;; --------------------------------

handle_mov:
    CALL write_rex_w
    MOV  AL, [rel mov_opcode]
    MOV [rel opcode], AL
    ADD R15, 4 ;; SKIP "MOV " IN INSTRUCTION
    SUB R14, 4 ;; ADJUST BYTES LEFT
    CALL handle_reg
    ADD R15, 2 ;; SKIP COMMA AND SPACE
    SUB R14, 2 ;; ADJUST BYTES LEFT
    CALL handle_hex
    CALL write_imm64
    RET
.done:
    RET

write_rex_w:
    ;; --------------------------------
    ;; WRITE(RAX = 1, RDI = fd, RSI = buffer, RDX = count)
    MOV RAX, 1 ;; SYS_WRITE
    MOV RDI, R13 ;; FD
    LEA RSI, [rel rex_w] ;; ADDRESS OF REX.W IN BUFFER
    MOV RDX, 1 ;; BYTES TO WRITE
    SYSCALL
    RET


    ;; --------------------------------
handle_hex:
    MOV QWORD [value], 0

.hex_loop:
    CMP R14, 0
    JE .done
    MOV AL, [R15]
    CMP AL, ' '
    JE .advance
    CMP AL, 0x0A
    JE .done
    JMP .convert

.advance:
    INC R15
    DEC R14
    JMP .hex_loop

.convert:
    CALL .hex_to_val
    MOVZX RAX, AL
    SHL QWORD [value], 4
    OR QWORD [value], RAX
    INC R15
    DEC R14
    JMP .hex_loop

.hex_to_val:
    CMP AL, '0'
    JB .invalid
    CMP AL, '9'
    JBE .num
    CMP AL, 'A'
    JB .invalid
    CMP AL, 'F'
    JBE .upper
.invalid:
    XOR EAX, EAX
    RET
.num:
    SUB AL, '0'
    RET
.upper:
    SUB AL, 'A'
    ADD AL, 10
    RET

.done:
    MOV RAX, [rel value]
    RET

handle_reg:
    MOV AL, [rel opcode]
    CMP BYTE [R15], 'R'
    JNE .done
    CMP BYTE [R15+1], 'A'
    JE .reg_rax
    CMP BYTE [R15+1], 'C'
    JE .reg_rcx
    JMP .done

.reg_rax:
    ADD AL, [rel rax_opcode]
    JMP .done

.reg_rcx:
    ADD AL, [rel rcx_opcode]

.done:
    MOV [rel opcode], AL
    CALL write_opcode
    ADD R15, 3
    SUB R14, 3
    RET

write_opcode:
    MOV RAX, 1 ;; SYS_WRITE
    MOV RDI, R13 ;; FD
    LEA RSI, [rel opcode] ;; ADDRESS OF OPCODE IN BUFFER
    MOV RDX, 1 ;; BYTES TO WRITE
    SYSCALL
    RET

write_imm64:
    MOV RAX, 1
    MOV RDI, R13
    LEA RSI, [rel value]
    MOV RDX, 8
    SYSCALL
    RET