DEFAULT REL

SECTION .data
filename         DB "test.asm", 0
o_filename       DB "test.bin", 0
rex_w            DB 0x48
syscall_opcode   DB 0x0F, 0x05
mov_opcode       DB 0xB8
mov_reg_opcode   DB 0x89
push_opcode      DB 0x50
pop_opcode       DB 0x58
rax_opcode       DB 0x00
rcx_opcode       DB 0x01
rdx_opcode       DB 0x02
rbx_opcode       DB 0x03
rsp_opcode       DB 0x04
rbp_opcode       DB 0x05
rsi_opcode       DB 0x06
rdi_opcode       DB 0x07

ELF_HEADER:
    db 0x7F,"ELF"
    db 2
    db 1
    db 1
    times 9 db 0
    dw 2
    dw 0x3E
    dd 1
    dq 0x400078
    dq 64
    dq 0
    dd 0
    dw 64
    dw 56
    dw 1
    dw 0
    dw 0

PROGRAM_HEADER:
    dd 1
    dd 5
    dq 0
    dq 0x400000
    dq 0x400000
    dq 0x1000
    dq 0x1000
    dq 0x1000
    
SECTION .bss
buffer  resb 1024
opcode  resb 1
value   resq 1
digit   resb 1
modrm   resb 1
src_reg resb 1
dst_reg resb 1

SECTION .text
GLOBAL _start

_start:
    ;; --------------------------------
    ;; OPEN(RAX = 2, RDI = filename, RSI = flags, RDX = mode)

    MOV RAX, 2                  ;; SYS_OPEN
    LEA RDI, [rel filename]     ;; FILE TO OPEN (RIP-relative)
    XOR RSI, RSI                ;; FLAGS = O_RDONLY (0)
    XOR RDX, RDX                ;; MODE = (not used)
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
    CALL write_elf_header
read_loop:
    ;; --------------------------------
    ;; READ(RAX = 0, RDI = fd, RSI = buffer, RDX = count)
    MOV RAX, 0              ;; SYS_READ
    MOV RDI, R12            ;; FD
    LEA RSI, [rel buffer]
    MOV RDX, 1024           ;; COUNT
    SYSCALL

    TEST RAX, RAX
    JZ exit
    JS exit_error

    MOV R14, RAX            ;; BYTES READ
    LEA R15, [rel buffer]   ;; BUFFER POINTER
    ;; --------------------------------
scan_loop:
    CMP R14, 1
    JL read_loop

check_syscall:
    CMP R14, 7
    JL check_mov

    CMP BYTE [R15], 'S'
    JNE check_mov
    CMP BYTE [R15+1], 'Y'
    JNE check_mov
    CMP BYTE [R15+2], 'S'
    JNE check_mov
    CMP BYTE [R15+3], 'C'
    JNE check_mov
    CMP BYTE [R15+4], 'A'
    JNE check_mov
    CMP BYTE [R15+5], 'L'
    JNE check_mov
    CMP BYTE [R15+6], 'L'
    JNE check_mov

    CALL handle_syscall
    JMP scan_loop

check_mov:
    CMP R14, 3
    JL check_push

    CMP BYTE [R15], 'M'
    JNE check_push
    CMP BYTE [R15+1], 'O'
    JNE check_push
    CMP BYTE [R15+2], 'V'
    JNE check_push

    JMP check_mov_reg

check_mov_reg:
    CMP R14, 4
    JL skip

    CMP BYTE [R15+4], 'R'
    JNE skip
    CMP BYTE [R15+9], 'R'
    JNE mov_non_reg

    CALL handle_mov_reg
    JMP scan_loop

mov_non_reg:
    CALL handle_mov
    JMP scan_loop

check_push:
    CMP R14, 4
    JL skip

    CMP BYTE [R15], 'P'
    JNE check_pop
    CMP BYTE [R15+1], 'U'
    JNE check_pop
    CMP BYTE [R15+2], 'S'
    JNE check_pop
    CMP BYTE [R15+3], 'H'
    JNE check_pop

    CALL handle_push
    JMP scan_loop

check_pop:
    CMP R14, 3
    JL skip

    CMP BYTE [R15], 'P'
    JNE check_xor
    CMP BYTE [R15+1], 'O'
    JNE check_xor
    CMP BYTE [R15+2], 'P'
    JNE check_xor

    CALL handle_pop
    JMP scan_loop

check_xor:
    CMP R14, 3
    JL skip

    CMP BYTE [R15], 'X'
    JNE skip
    CMP BYTE [R15+1], 'O'
    JNE skip
    CMP BYTE [R15+2], 'R'
    JNE skip

    CALL handle_xor
    JMP scan_loop

skip:
    INC R15
    DEC R14
    JMP scan_loop

exit:
    ;; --------------------------------
    ;; CLOSE(RAX = 3, RDI = fd)
    MOV RAX, 3                  ;; SYS_CLOSE
    MOV RDI, R12                ;; FD
    SYSCALL
    ;; --------------------------------

    ;; --------------------------------
    ;; CLOSE(RAX = 3, RDI = fd)
    MOV RAX, 3                  ;; SYS_CLOSE
    MOV RDI, R13                ;; FD
    SYSCALL
    ;; --------------------------------

    ;; --------------------------------
    ;; EXIT(RAX = 60, RDI = status)
    MOV RAX, 60                 ;; SYS_EXIT
    XOR RDI, RDI                ;; STATUS = 0
    SYSCALL
    ;; --------------------------------
 
exit_error:
    ;; --------------------------------
    ;; EXIT(RAX = 60, RDI = status)
    MOV RAX, 60                 ;; SYS_EXIT
    MOV RDI, 1                  ;; STATUS = 0
    SYSCALL
    ;; --------------------------------

handle_xor:

    ADD R15, 4
    SUB R14, 4

    CALL parse_reg
    MOV [dst_reg], AL

    ADD R15, 3
    SUB R14, 3

    ADD R15, 2
    SUB R14, 2

    CALL parse_reg
    MOV [src_reg], AL

    ADD R15, 3
    SUB R14, 3

    CALL write_rex_w

    MOV BYTE [opcode], 0x31
    CALL write_opcode

    CALL encode_modrm
    CALL write_modrm

    RET

handle_pop:
    MOV AL, [rel pop_opcode]
    MOV [rel opcode], AL
    ADD R15, 4                    ;; SKIP "POP " IN INSTRUCTION
    SUB R14, 4                    ;; ADJUST BYTES LEFT
    CALL handle_reg
    ADD R15, 3                    ;; SKIP REGISTER NAME
    SUB R14, 3                    ;; ADJUST BYTES LEFT
    RET

handle_push:
    MOV AL, [rel push_opcode]
    MOV [rel opcode], AL
    ADD R15, 5                    ;; SKIP "PUSH " IN INSTRUCTION
    SUB R14, 5                    ;; ADJUST BYTES LEFT
    CALL handle_reg
    ADD R15, 3                    ;; SKIP REGISTER NAME
    SUB R14, 3                    ;; ADJUST BYTES LEFT
    RET

handle_syscall:
    ;; --------------------------------
    ;; WRITE(RAX = 1, RDI = fd, RSI = buffer, RDX = count)
    MOV RAX, 1                      ;; SYS_WRITE
    MOV RDI, R13                    ;; FD
    LEA RSI, [rel syscall_opcode]   ;; SYSCALL OPCODE
    MOV RDX, 2                      ;; BYTES TO WRITE
    SYSCALL

    ADD R15, 7
    SUB R14, 7

    RET
    ;; --------------------------------

handle_mov_reg:
    ADD R15, 4
    SUB R14, 4
    CALL parse_reg
    MOV [rel dst_reg], AL

    ADD R15, 3
    SUB R14, 3

    ADD R15, 2
    SUB R14, 2

    CALL parse_reg
    MOV [rel src_reg], AL

    ADD R15, 3
    SUB R14, 3

    CALL write_rex_w

    MOV BYTE [rel opcode], 0x89
    CALL write_opcode

    CALL encode_modrm
    CALL write_modrm

    RET

handle_mov:
    CALL write_rex_w
    MOV  AL, [rel mov_opcode]
    MOV [rel opcode], AL
    ADD R15, 4                    ;; SKIP "MOV " IN INSTRUCTION
    SUB R14, 4                    ;; ADJUST BYTES LEFT
    CALL handle_reg
    ADD R15, 2                    ;; SKIP COMMA AND SPACE
    SUB R14, 2                    ;; ADJUST BYTES LEFT
    CALL handle_hex
    CALL write_imm64
    
    RET
.done:
    RET

write_rex_w:
    ;; --------------------------------
    ;; WRITE(RAX = 1, RDI = fd, RSI = buffer, RDX = count)
    MOV RAX, 1                    ;; SYS_WRITE
    MOV RDI, R13                  ;; FD
    LEA RSI, [rel rex_w]          ;; ADDRESS OF REX.W IN BUFFER
    MOV RDX, 1                    ;; BYTES TO WRITE
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
    CMP BYTE [R15+1], 'D'
    JE .reg_rd
    CMP BYTE [R15+1], 'S'
    JE .reg_rs
    CMP BYTE [R15+1], 'B'
    JE .reg_rb
    JMP .done
.reg_rs:
    CMP BYTE [R15+2], 'P'
    JE .reg_rsp
    CMP BYTE [R15+2], 'I'
    JE .reg_rsi
    JMP .done
.reg_rb:
    CMP BYTE [R15+2], 'P'
    JE .reg_rbp
    CMP BYTE [R15+2], 'X'
    JE .reg_rbx
    JMP .done
.reg_rd:
    CMP BYTE [R15+2], 'X'
    JE .reg_rdx
    CMP BYTE [R15+2], 'I'
    JE .reg_rdi
    JMP .done
.reg_rax:
    ADD AL, [rel rax_opcode]
    JMP .done

.reg_rcx:
    ADD AL, [rel rcx_opcode]
    JMP .done
.reg_rdx:
    ADD AL, [rel rdx_opcode]
    JMP .done
.reg_rbx:
    ADD AL, [rel rbx_opcode]
    JMP .done
.reg_rsp:
    ADD AL, [rel rsp_opcode]
    JMP .done
.reg_rbp:
    ADD AL, [rel rbp_opcode]
    JMP .done
.reg_rsi:
    ADD AL, [rel rsi_opcode]
    JMP .done
.reg_rdi:
    ADD AL, [rel rdi_opcode]
.done:
    MOV [rel opcode], AL
    CALL write_opcode
    ADD R15, 3
    SUB R14, 3
    RET

write_opcode:
    MOV RAX, 1              ;; SYS_WRITE
    MOV RDI, R13            ;; FD
    LEA RSI, [rel opcode]   ;; ADDRESS OF OPCODE IN BUFFER
    MOV RDX, 1              ;; BYTES TO WRITE
    SYSCALL
    RET

write_imm64:
    MOV RAX, 1
    MOV RDI, R13
    LEA RSI, [rel value]
    MOV RDX, 8
    SYSCALL
    RET

encode_modrm:
    MOV AL, [rel src_reg]
    SHL AL, 3
    OR  AL, [rel dst_reg]
    OR  AL, 0xC0
    MOV [rel modrm], AL
    RET

write_modrm:
    MOV RAX, 1
    MOV RDI, R13
    LEA RSI, [rel modrm]
    MOV RDX, 1
    SYSCALL
    RET

parse_reg:

    CMP BYTE [R15+1], 'A'
    JE .rax
    CMP BYTE [R15+1], 'C'
    JE .rcx
    CMP BYTE [R15+1], 'D'
    JE .rdx_rdi
    CMP BYTE [R15+1], 'B'
    JE .rbx_rbp
    CMP BYTE [R15+1], 'S'
    JE .rsp_rsi

.rax:
    MOV AL, 0
    RET

.rcx:
    MOV AL, 1
    RET

.rdx_rdi:
    CMP BYTE [R15+2], 'X'
    JE .rdx
    MOV AL, 7
    RET
.rdx:
    MOV AL, 2
    RET

.rbx_rbp:
    CMP BYTE [R15+2], 'X'
    JE .rbx
    MOV AL, 5
    RET
.rbx:
    MOV AL, 3
    RET

.rsp_rsi:
    CMP BYTE [R15+2], 'P'
    JE .rsp
    MOV AL, 6
    RET
.rsp:
    MOV AL, 4
    RET

write_elf_header:
    MOV RAX, 1
    MOV RDI, R13
    LEA RSI, [rel ELF_HEADER]
    MOV RDX, 64
    SYSCALL
    JMP .write_program_header
.write_program_header:
    MOV RAX, 1
    MOV RDI, R13
    LEA RSI, [rel PROGRAM_HEADER]
    MOV RDX, 56
    SYSCALL
    RET