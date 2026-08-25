/*
 * NPU 裸机冒烟测试 —— 在 -M virt,highmem=off 下直接驱动 NPU 设备：
 *   枚举 PCI 找到 NPU -> 分配 BAR0 -> MMIO 读 ID -> 命令队列(ADD) -> DMA(COPY) -> 读结果
 *
 * 编译（macOS，clang 交叉到 aarch64 bare-metal）：
 *   clang --target=aarch64-none-elf -ffreestanding -nostdlib -nostartfiles \
 *         -fno-stack-protector -O2 -Wl,-Ttext=0x40000000 -Wl,-e,_start \
 *         -o test_npu.elf crt0.S test_npu.c
 * 运行：
 *   qemu-system-aarch64 -M virt,highmem=off -cpu cortex-a53 -m 256M \
 *       -device npu -kernel test_npu.elf -nographic
 */
#include <stdint.h>

#define UART0   0x09000000UL   /* PL011 */
#define ECAM    0x3f000000UL   /* PCIe ECAM (highmem=off) */
#define NPU_BAR 0x10000000UL   /* 分配给 NPU 的 BAR0 地址 */

enum { REG_ID = 0x00, REG_STATUS = 0x04, REG_RESULT = 0x08,
       REG_CMDQ = 0x10, REG_DOORBELL = 0x14 };
enum { OP_ADD = 0x01, OP_COPY = 0x03 };
#define STAT_DONE 0x2

/* 与设备/功能模型一致的命令描述符布局 */
typedef struct {
    uint32_t opcode, flags;
    uint64_t src, dst;
    uint32_t a, b, count, reserved;
} NPUCommand;

static void uart_putc(char c)
{
    volatile uint32_t *dr = (uint32_t *)(UART0 + 0x00);
    volatile uint32_t *fr = (uint32_t *)(UART0 + 0x18);
    while (*fr & (1 << 5));      /* 等 TX FIFO 不满 */
    *dr = c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(uint32_t v)
{
    static const char h[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(h[(v >> i) & 0xf]);
}

static uint32_t ecam_rd(int dev, int off) { return *(volatile uint32_t *)(ECAM + dev * 0x8000 + off); }
static void ecam_wr(int dev, int off, uint32_t v) { *(volatile uint32_t *)(ECAM + dev * 0x8000 + off) = v; }

int main(void)
{
    volatile uint32_t *npu = (uint32_t *)NPU_BAR;
    volatile NPUCommand *cmd = (NPUCommand *)0x40010000UL;
    volatile uint32_t *src = (uint32_t *)0x40020000UL;
    volatile uint32_t *dst = (uint32_t *)0x40030000UL;
    int dev = -1, i, ok;

    /* 1. 在 bus0 上找 NPU (vendor 0x1234, device 0x0001) */
    for (i = 0; i < 32; i++) {
        uint32_t v = ecam_rd(i, 0x00);
        if (v == 0xffffffff) continue;
        if ((v & 0xffff) == 0x1234 && ((v >> 16) & 0xffff) == 0x0001) { dev = i; break; }
    }
    if (dev < 0) { uart_puts("FAIL: NPU not found\n"); return 1; }
    uart_puts("NPU at bus0 dev"); uart_hex(dev); uart_puts("\n");

    /* 2. 分配 BAR0 到 0x10000000，并使能 memory space + bus master */
    ecam_wr(dev, 0x10, NPU_BAR);
    ecam_wr(dev, 0x04, 0x6);   /* 命令寄存器：bit1=memory space, bit2=bus master */

    /* 3. 读 ID */
    uart_puts("ID = "); uart_hex(npu[REG_ID / 4]);
    uart_puts((npu[REG_ID / 4] == 0x4e505531) ? "  OK\n" : "  MISMATCH\n");

    /* 4. ADD 3 + 4 = 7 */
    cmd->opcode = OP_ADD; cmd->flags = 0; cmd->src = 0; cmd->dst = 0;
    cmd->a = 3; cmd->b = 4; cmd->count = 0; cmd->reserved = 0;
    npu[REG_CMDQ / 4] = 0x40010000;
    npu[REG_DOORBELL / 4] = 1;
    while (npu[REG_STATUS / 4] != STAT_DONE) ;
    uart_puts("ADD 3+4 = "); uart_hex(npu[REG_RESULT / 4]);
    uart_puts((npu[REG_RESULT / 4] == 7) ? "  OK\n" : "  FAIL\n");

    /* 5. COPY 8 个字 (DMA) */
    for (i = 0; i < 8; i++) { src[i] = 0x1000 + i; dst[i] = 0; }
    cmd->opcode = OP_COPY; cmd->flags = 0;
    cmd->src = 0x40020000; cmd->dst = 0x40030000;
    cmd->a = 0; cmd->b = 0; cmd->count = 8; cmd->reserved = 0;
    npu[REG_CMDQ / 4] = 0x40010000;
    npu[REG_DOORBELL / 4] = 1;
    while (npu[REG_STATUS / 4] != STAT_DONE) ;
    ok = 1;
    for (i = 0; i < 8; i++) if (dst[i] != 0x1000 + i) ok = 0;
    uart_puts("COPY 8 words: "); uart_puts(ok ? "OK\n" : "FAIL\n");

    uart_puts("ALL TESTS DONE\n");
    return 0;
}
