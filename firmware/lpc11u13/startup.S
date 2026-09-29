.syntax unified
.cpu cortex-m0
.thumb

.section .vectors,"a",%progbits
.global vectors
vectors:
    .word _stack_top
    .word Reset_Handler
    .word Default_Handler
    .word Default_Handler
    .word 0, 0, 0
    .word 0 /* NXP checksum is populated in the output .bin by build_lpc.py. */
    .word 0, 0, 0
    .word Default_Handler
    .word 0, 0
    .word Default_Handler
    .word SysTick_Handler
    .word PIN_INT0_Handler
    .rept 31
    .word Default_Handler
    .endr

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    cpsid i
    ldr r0, =0x40048000
    movs r1, #2
    str r1, [r0] /* SYSMEMREMAP = Flash. */
    dsb
    isb
    ldr r0, =_data_load
    ldr r1, =_data_start
    ldr r2, =_data_end
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0]
    str r3, [r1]
    adds r0, #4
    adds r1, #4
    b 1b
2:  ldr r1, =_bss_start
    ldr r2, =_bss_end
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1]
    adds r1, #4
    b 3b
4:  bl main
    b Default_Handler

.global Default_Handler
.type Default_Handler,%function
.thumb_func
Default_Handler:
    b Default_Handler

/* Capture the start time before the C prologue. Cortex-M0 exception entry
 * takes at least 16 cycles; compensate that fixed portion of the RX delay. */
.global PIN_INT0_Handler
.type PIN_INT0_Handler,%function
.thumb_func
PIN_INT0_Handler:
    ldr r0, =0x40018008
    ldr r0, [r0]
    subs r0, #16
    b uartReceive
