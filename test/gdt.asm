[BITS 16]

SECTION .data
GLOBAL gdt
GLOBAL gdt_descriptor
GLOBAL CODE_SEG
GLOBAL DATA_SEG

CODE_SEG EQU gdt_code - gdt    ;; = 0x08
DATA_SEG EQU gdt_data - gdt    ;; = 0x10

gdt:
gdt_null:
    DQ 0x0000000000000000       ;; Null descriptor

gdt_code:
    DW 0xFFFF                   ;; Limit [0:15]
    DW 0x0000                   ;; Base  [0:15]
    DB 0x00                     ;; Base  [16:23]
    DB 10011010b                ;; Access: present, ring 0, code, exec, readable
    DB 11001111b                ;; Flags: 4KB gran, 32-bit protected; Limit [16:19]
    DB 0x00                     ;; Base  [24:31]

gdt_data:
    DW 0xFFFF                   ;; Limit [0:15]
    DW 0x0000                   ;; Base  [0:15]
    DB 0x00                     ;; Base  [16:23]
    DB 10010010b                ;; Access: present, ring 0, data, writable
    DB 11001111b                ;; Flags: 4KB gran, 32-bit protected; Limit [16:19]
    DB 0x00                     ;; Base  [24:31]

gdt_end:

gdt_descriptor:
    DW gdt_end - gdt - 1        ;; GDT size minus 1
    DD 0x00000000               ;; GDT base address (filled at runtime by boot2)
