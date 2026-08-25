/* 裸机启动入口：设置栈指针后跳转 main（aarch64，MMU 关闭） */
.section .text
.globl _start
_start:
    movz x0, #0x4100, lsl #16   /* sp = 0x41000000（RAM 内，避开测试缓冲区） */
    mov sp, x0
    bl main
1:
    wfi
    b 1b
