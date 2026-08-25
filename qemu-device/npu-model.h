/*
 * NPU 功能模型（骨架）—— 纯 C 参考实现，不依赖 QEMU。
 *
 * 这里就是你的 NPU「指令集语义」落地的地方：
 *   1. 在 opcode 枚举里加你的真实指令；
 *   2. 在 npu_model_exec() 里实现每条指令的语义；
 *   3. 如果指令需要访存，用 rd/wr 回调读写 guest 内存（DMA）。
 *
 * 这个文件是黄金参考模型：以后做 RTL 协同仿真时，同一份激励
 * 喂给 RTL 和这里，比对结果即可。
 */
#ifndef NPU_MODEL_H
#define NPU_MODEL_H

#include <stdint.h>

/* NPU 状态 */
#define NPU_STAT_IDLE   0x0
#define NPU_STAT_BUSY   0x1
#define NPU_STAT_DONE   0x2
#define NPU_STAT_ERR    0x3

/* 推理相关的维度上限（够用即可，防止过大的栈分配） */
#define NPU_MAX_DIM     64

/* 示例 opcode —— 替换成你的真实 ISA */
enum {
    NPU_OP_NOP   = 0x00,   /* 空操作                          */
    NPU_OP_ADD   = 0x01,   /* result = a + b                  */
    NPU_OP_MUL   = 0x02,   /* result = a * b                  */
    NPU_OP_COPY  = 0x03,   /* 从 src 搬 count 个 u32 到 dst    */
    NPU_OP_INFER = 0x04,   /* 全连接层：out[j]=Σ W[j,i]·in[i]+bias[j]，
                              result = argmax(out)。输入/权重/偏置均为 int32 */
};

/*
 * 命令描述符：由驱动写入 guest 内存，设备通过 DMA 读取。
 * 布局必须与 guest 侧驱动保持一致（见 npu_drv / 测试程序）。
 *
 * 字段按 opcode 复用：
 *   ADD/MUL : a、b 为操作数
 *   COPY    : src/dst 为搬运地址，count 为字数
 *   INFER   : src=输入向量(N个int32)、dst=输出分数(M个int32)、
 *             weights=权重矩阵(N*M个int32,行主序)、bias=偏置(M个int32)、
 *             a=N 输入维度、b=M 类别数
 */
typedef struct NPUCommand {
    uint32_t opcode;
    uint32_t flags;
    uint64_t src;      /* guest 物理地址 */
    uint64_t dst;      /* guest 物理地址 */
    uint32_t a;        /* 操作数 / 输入维度 N */
    uint32_t b;        /* 操作数 / 类别数 M  */
    uint32_t count;    /* 搬运字数 (COPY)    */
    uint32_t reserved;
    uint64_t weights;  /* INFER: 权重矩阵 guest 物理地址 */
    uint64_t bias;     /* INFER: 偏置向量 guest 物理地址 */
} NPUCommand;

/* 功能模型内部状态 */
typedef struct NPUModel {
    uint32_t status;
    uint32_t result;
    uint64_t cmd_count;   /* 已执行命令数 */
} NPUModel;

/* guest 内存读写回调：由设备层实现（QEMU 里用 address_space 读写） */
typedef int (*NPUReadFn)(void *ctx, uint64_t gpa, void *buf, uint32_t len);
typedef int (*NPUWriteFn)(void *ctx, uint64_t gpa, const void *buf, uint32_t len);

void npu_model_init(NPUModel *m);
int  npu_model_exec(NPUModel *m, const NPUCommand *cmd,
                    void *ctx, NPUReadFn rd, NPUWriteFn wr);

#endif /* NPU_MODEL_H */
