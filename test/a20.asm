[BITS 16]

SECTION .text
GLOBAL a20

;; Enable the A20 line.
;; Method 1: BIOS INT 15h, AX=2401h
;; Method 2: Fast A20 via system control port 0x92
a20:
    MOV AX, 0x2401
    INT 0x15
    JNC .done           ;; CF clear = success

    ;; Fast A20: set bit 1, preserve bit 0 (bit 0 = system reset, must stay 0)
    IN  AL, 0x92
    OR  AL, 0x02
    AND AL, 0xFE
    OUT 0x92, AL

.done:
    RET
