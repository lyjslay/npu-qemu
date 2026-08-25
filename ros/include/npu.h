/*
 * NPU 驱动与用户态共享的接口定义。
 * 内核模块和用户态程序都 include 这个头文件，保证 ioctl 编号和结构体布局一致。
 */
#ifndef NPU_H
#define NPU_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif
#include <linux/ioctl.h>

#define NPU_MAX_DIM 64

#define NPU_IOCTL_MAGIC 'N'

/*
 * NPU_IOCTL_INFER：执行一次全连接推理。
 *   输入: input(N个int32), weights(N*M个int32行主序), bias(M个int32)
 *   输出: output(M个分数int32), result=argmax(output)
 */
struct npu_infer_args {
    const int32_t *input;
    int32_t *output;
    const int32_t *weights;
    const int32_t *bias;
    uint32_t n_inputs;
    uint32_t n_classes;
    int32_t result;       /* 输出：分类结果（argmax 索引） */
};

#define NPU_IOCTL_INFER _IOWR(NPU_IOCTL_MAGIC, 1, struct npu_infer_args)

#endif /* NPU_H */
