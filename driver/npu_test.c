/*
 * NPU 用户态测试程序：打开 /dev/npu，通过 NPU_IOCTL_INFER 跑推理。
 * 编译（guest 内）：gcc -O2 -o npu_test npu_test.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include "npu.h"
#include "sensor_model.h"

static int do_infer(int fd, const int32_t *input)
{
    int32_t output[N_CLASSES];
    struct npu_infer_args args;

    memset(&args, 0, sizeof(args));
    args.input = input;
    args.output = output;
    args.weights = weights;
    args.bias = bias;
    args.n_inputs = N_INPUTS;
    args.n_classes = N_CLASSES;

    if (ioctl(fd, NPU_IOCTL_INFER, &args) < 0) {
        perror("ioctl");
        return -1;
    }

    printf("  input=[%d,%d,%d,%d] -> class=%d (%s) scores=[%d,%d,%d]\n",
           input[0], input[1], input[2], input[3],
           args.result, class_names[args.result],
           output[0], output[1], output[2]);
    return args.result;
}

int main(void)
{
    int fd = open("/dev/npu", O_RDWR);
    if (fd < 0) {
        perror("open /dev/npu");
        return 1;
    }

    int32_t samples[3][4] = {
        { 500,   800, 40, 3300 },   /* 期望 normal   (0) */
        { 800,   900, 40, 3300 },   /* 期望 warning  (1) */
        { 1200, 1000, 45, 3300 },   /* 期望 critical (2) */
    };
    int expected[3] = { 0, 1, 2 };
    int pass = 1;

    for (int i = 0; i < 3; i++) {
        int r = do_infer(fd, samples[i]);
        if (r != expected[i]) {
            printf("  !! 期望 class=%d\n", expected[i]);
            pass = 0;
        }
    }

    printf(pass ? "ALL INFER TESTS PASSED\n" : "SOME TESTS FAILED\n");
    close(fd);
    return pass ? 0 : 1;
}
