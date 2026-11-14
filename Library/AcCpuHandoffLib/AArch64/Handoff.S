// Position-independent EL1 entry and Slbounce EL2 transition.
#include "../HandoffInternal.h"

.text
.balign 4096
.global AcHandoffCodeStart
.global AcHandoffCodeEnd
.global AcHandoffStart
.global AcHandoffDirect
.global AcHandoffAbort
.global AcHandoffContextSlot
AcHandoffCodeStart:
    hint    #38
    // Slbounce enters at EL1 with x0 pointing to sl_tb_data.
    mov     x9, x0
    hvc     #1
    msr     daifset, #15
    dsb     sy
    mrs     x10, sctlr_el2
    bic     x10, x10, #1
    bic     x10, x10, #4
    bic     x10, x10, #0x1000
    msr     sctlr_el2, x10
    isb
    ldr     x19, [x9]
    msr     spsel, #1
    ldr     x10, [x19, #AC_HANDOFF_STACK]
    mov     sp, x10
    adr     x10, AcHandoffVectors2
    msr     vbar_el2, x10
    mov     x10, #AC_HANDOFF_STAGE_EL2
    str     x10, [x19, #AC_HANDOFF_STAGE]
    // HCR_EL2.RW, API and APK; no VHE or stage-2 translation.
    mov     x10, #(1 << 31)
    orr     x10, x10, #(1 << 40)
    orr     x10, x10, #(1 << 41)
    msr     hcr_el2, x10
    isb
    mov     x10, #3
    msr     cnthctl_el2, x10
    msr     cntvoff_el2, xzr
    msr     hstr_el2, xzr
    msr     vttbr_el2, xzr
    mov     x10, #0x33ff
    msr     cptr_el2, x10
    mrs     x10, sctlr_el1
    bic     x10, x10, #1
    bic     x10, x10, #4
    bic     x10, x10, #0x1000
    msr     sctlr_el1, x10
    ldr     x10, [x19, #AC_HANDOFF_STACK]
    msr     sp_el1, x10
    adr     x10, AcHandoffEl1
    msr     elr_el2, x10
    mov     x10, #0x3c5
    msr     spsr_el2, x10
    dsb     sy
    isb
    eret

AcHandoffStart:
    hint    #38
    msr     daifset, #15
    msr     spsel, #1
    mov     x19, x0
    ldr     x10, [x19, #AC_HANDOFF_STACK]
    mov     sp, x10
    mov     x29, xzr
    adr     x10, AcHandoffVectors1
    msr     vbar_el1, x10
    isb
    br      x1

AcHandoffDirect:
    hint    #38
    mov     x19, x0
    msr     daifset, #15
    mov     x10, #AC_HANDOFF_STAGE_EL1
    str     x10, [x19, #AC_HANDOFF_STAGE]
    bl      AcHandoffCleanState
    mrs     x10, sctlr_el1
    bic     x10, x10, #1
    bic     x10, x10, #4
    bic     x10, x10, #0x1000
    dsb     sy
    msr     sctlr_el1, x10
    isb
AcHandoffEl1:
    mov     x10, #AC_HANDOFF_STAGE_EL1
    str     x10, [x19, #AC_HANDOFF_STAGE]
    msr     spsel, #1
    ldr     x10, [x19, #AC_HANDOFF_STACK]
    mov     sp, x10
    adr     x10, AcHandoffVectors1
    msr     vbar_el1, x10
    msr     cntp_ctl_el0, xzr
    msr     cntv_ctl_el0, xzr
    msr     tpidr_el0, xzr
    msr     tpidrro_el0, xzr
    msr     tpidr_el1, xzr
    mov     x10, #(3 << 20)
    msr     cpacr_el1, x10
    tlbi    vmalle1
    ic      iallu
    dsb     sy
    isb
    mov     x10, #AC_HANDOFF_STAGE_BRANCH
    str     x10, [x19, #AC_HANDOFF_STAGE]
    ldr     x16, [x19, #AC_HANDOFF_ENTRY]
    ldr     x0, [x19, #AC_HANDOFF_ARGUMENT]
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x29, xzr
    adr     x30, AcHandoffReturned
    dsb     sy
    br      x16
AcHandoffReturned:
    adr     x19, AcHandoffContextSlot
    ldr     x19, [x19]
    mov     x10, #-1
    str     x10, [x19, #AC_HANDOFF_STATUS]
    b       AcHandoffRender

AcHandoffAbort:
    hint    #38
    mov     x19, x0
    msr     daifset, #15
    adr     x10, AcHandoffVectors1
    msr     vbar_el1, x10
    isb
    bl      AcHandoffCleanState
    mrs     x10, sctlr_el1
    bic     x10, x10, #1
    bic     x10, x10, #4
    bic     x10, x10, #0x1000
    dsb     sy
    msr     sctlr_el1, x10
    isb
    b       AcHandoffRender

AcHandoffCleanState:
    // Publish the private context and stack before disabling EL1 translation.
    mrs     x10, ctr_el0
    ubfx    x10, x10, #16, #4
    mov     x11, #4
    lsl     x11, x11, x10
    sub     x12, x11, #1
    bic     x10, x19, x12
    add     x12, x19, #(AC_HANDOFF_DATA_PAGES * 4096)
1:  dc      cvac, x10
    add     x10, x10, x11
    cmp     x10, x12
    b.lo    1b
    dsb     sy
    ret

AcHandoffException1:
    msr     daifset, #15
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    mov     x4, #1
    mrs     x10, sctlr_el1
    bic     x10, x10, #1
    bic     x10, x10, #4
    bic     x10, x10, #0x1000
    dsb     sy
    msr     sctlr_el1, x10
    isb
    b       AcHandoffException
AcHandoffException2:
    msr     daifset, #15
    mrs     x1, esr_el2
    mrs     x2, elr_el2
    mrs     x3, far_el2
    mov     x4, #2
    mrs     x10, sctlr_el2
    bic     x10, x10, #1
    bic     x10, x10, #4
    bic     x10, x10, #0x1000
    dsb     sy
    msr     sctlr_el2, x10
    isb
AcHandoffException:
    adr     x19, AcHandoffContextSlot
    ldr     x19, [x19]
    stp     x1, x2, [x19, #AC_HANDOFF_ESR]
    str     x3, [x19, #AC_HANDOFF_FAR]
    str     x4, [x19, #AC_HANDOFF_EXCEPTION_EL]
    b       AcHandoffRender

.balign 2048
AcHandoffVectors1:
.rept 16
    b       AcHandoffException1
    .space  124
.endr
.balign 2048
AcHandoffVectors2:
.rept 16
    b       AcHandoffException2
    .space  124
.endr

AcHandoffRender:
    // Six hexadecimal rows: stage, status, exception EL, ESR, ELR and FAR.
    ldr     x20, [x19, #AC_HANDOFF_FRAME]
    cbz     x20, AcHandoffHalt
    ldr     x21, [x19, #AC_HANDOFF_ROW_BYTES]
    add     x20, x20, #64
    add     x20, x20, x21, lsl #4
    mov     x22, #0
1:  adr     x0, AcHandoffRowOffsets
    ldrb    w0, [x0, x22]
    ldr     x23, [x19, x0]
    mov     x24, #0
2:  mov     x0, #60
    sub     x0, x0, x24, lsl #2
    lsr     x0, x23, x0
    and     x0, x0, #15
    adr     x1, AcHandoffHex
    ldrh    w25, [x1, x0, lsl #1]
    mov     x26, #0
3:  mov     x27, #0
4:  mov     x0, #14
    sub     x0, x0, x26, lsl #1
    sub     x0, x0, x26
    sub     x0, x0, x27
    lsr     x0, x25, x0
    and     x0, x0, #1
    neg     w0, w0
    and     w0, w0, #0xffffff
    mov     x1, #12
    lsl     x2, x26, #1
    madd    x2, x22, x1, x2
    mul     x2, x2, x21
    lsl     x3, x27, #1
    madd    x1, x24, x1, x3
    add     x2, x2, x1, lsl #2
    add     x2, x20, x2
    stp     w0, w0, [x2]
    add     x2, x2, x21
    stp     w0, w0, [x2]
    add     x27, x27, #1
    cmp     x27, #3
    b.lo    4b
    add     x26, x26, #1
    cmp     x26, #5
    b.lo    3b
    add     x24, x24, #1
    cmp     x24, #16
    b.lo    2b
    add     x22, x22, #1
    cmp     x22, #6
    b.lo    1b
AcHandoffHalt:
    msr     daifset, #15
    dsb     sy
1:  wfe
    b       1b

AcHandoffRowOffsets:
    .byte AC_HANDOFF_STAGE, AC_HANDOFF_STATUS, AC_HANDOFF_EXCEPTION_EL
    .byte AC_HANDOFF_ESR, AC_HANDOFF_ELR, AC_HANDOFF_FAR
.balign 2
AcHandoffHex:
    .hword 0b111101101101111, 0b010110010010111
    .hword 0b111001111100111, 0b111001111001111
    .hword 0b101101111001001, 0b111100111001111
    .hword 0b111100111101111, 0b111001001001001
    .hword 0b111101111101111, 0b111101111001111
    .hword 0b010101111101101, 0b110101110101110
    .hword 0b111100100100111, 0b110101101101110
    .hword 0b111100110100111, 0b111100110100100
.balign 8
AcHandoffContextSlot:
    .quad 0
AcHandoffCodeEnd:

.global InternalHandoffSmc
InternalHandoffSmc:
    hint    #34
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    dsb     sy
    smc     #0
    ret
