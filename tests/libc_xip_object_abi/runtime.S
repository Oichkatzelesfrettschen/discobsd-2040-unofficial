.syntax unified
.thumb
.text
.global fixture_set_r9
.type fixture_set_r9,%function
.thumb_func
fixture_set_r9:
    mov r9, r0
    bx lr
.size fixture_set_r9, .-fixture_set_r9

/* Linux-EABI calls execute only inside qemu-arm user mode. */
.global fixture_map_flash
.type fixture_map_flash,%function
.thumb_func
fixture_map_flash:
    push {r4, r5, r7, lr}
    movs r2, #3
    movs r3, #0x32
    movs r4, #0
    mvns r4, r4
    movs r5, #0
    movs r7, #192
    svc #0
    pop {r4, r5, r7, pc}
.size fixture_map_flash, .-fixture_map_flash

.global fixture_protect_flash
.type fixture_protect_flash,%function
.thumb_func
fixture_protect_flash:
    push {r7, lr}
    movs r2, #5
    movs r7, #125
    svc #0
    pop {r7, pc}
.size fixture_protect_flash, .-fixture_protect_flash

.global fixture_exit
.type fixture_exit,%function
.thumb_func
fixture_exit:
    movs r7, #1
    svc #0
    b .
.size fixture_exit, .-fixture_exit
.section .note.GNU-stack,"",%progbits
