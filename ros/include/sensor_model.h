/*
 * 演示用的「预训练」传感器分类模型（4 特征 → 3 类）。
 * 特征：温度 / 压力 / 湿度 / 电压（int32，已缩放）
 * 类别：normal(正常) / warning(警告) / critical(故障)
 *
 * 权重为行主序 weights[j*N+i]，bias 为各类偏置。
 * 真实场景里这里会是从模型文件加载的权重；此处为了演示手写了一个
 * 温度阈值线性分类器（temp<700 normal，700~1000 warning，>1000 critical）。
 */
#ifndef SENSOR_MODEL_H
#define SENSOR_MODEL_H

#include <stdint.h>

#define N_INPUTS  4
#define N_CLASSES 3

static const int32_t weights[N_CLASSES * N_INPUTS] = {
    -1, 0, 0, 0,   /* class 0: normal   = -temp + 1000 */
     1, 0, 0, 0,   /* class 1: warning  =  temp - 400  */
     2, 0, 0, 0,   /* class 2: critical = 2*temp - 1400 */
};

static const int32_t bias[N_CLASSES] = { 1000, -400, -1400 };

static const char *class_names[N_CLASSES] = { "normal", "warning", "critical" };

#endif /* SENSOR_MODEL_H */
