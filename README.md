# NPU QEMU 原型验证

用 QEMU 给自研 NPU 芯片做**软件栈功能验证**：在 QEMU 设备模型上跑 Ubuntu 24.04，
实现 NPU 驱动 + 用户态程序，安装 ROS 2 并运行节点用 NPU 推理发布 ROS 消息。

> 完整实现说明、所有命令和排错见 [实现说明书.md](./实现说明书.md)。

## 架构

```
ROS 节点 → ioctl(NPU_IOCTL_INFER) → /dev/npu → npu_drv.ko 驱动
        → MMIO → QEMU npu.c 设备模型 → npu-model.c 功能模型(矩阵乘+bias+argmax)
        → 分类结果 → ROS 节点 → publish /npu/result
```

## 目录结构

| 目录 | 内容 |
|------|------|
| `qemu-device/` | QEMU 自定义设备模型 `npu.c` + 功能模型 `npu-model.c/.h` |
| `driver/` | NPU 内核驱动 `npu_drv.c`、共享头 `npu.h`、模型参数 `sensor_model.h`、测试 `npu_test.c` |
| `bare-metal/` | 裸机冒烟测试源码（`crt0.S` + `test_npu.c`） |
| `ros/` | ROS 2 包 `npu_ros`（自定义消息 + rclcpp 推理节点） |
| `实现说明书.md` | 完整实现文档 |

## 快速开始

1. **QEMU 设备**：下载 QEMU 11.0.0 源码，把 `qemu-device/` 下三个文件复制到 `hw/misc/`，
   在 `hw/misc/meson.build` 加 `system_ss.add(files('npu.c', 'npu-model.c'))`，编译 aarch64-softmmu。
2. **镜像/固件**：下载 Ubuntu noble arm64 云镜像 + AAVMF UEFI 固件（详见说明书第 6 节）。
3. **驱动**：guest 内 `make -C /lib/modules/$(uname -r)/build M=$PWD modules` + `insmod npu_drv.ko`。
4. **ROS 节点**：`colcon build --packages-select npu_ros` + `ros2 run npu_ros npu_infer_node`。

详见 [实现说明书.md](./实现说明书.md)。

## 环境依赖（不进本仓库，需自行获取）

- QEMU 11.0.0 源码（本仓库只含自定义设备文件）
- Ubuntu 24.04 arm64 云镜像 `noble-server-cloudimg-arm64.img`
- EDK2/AAVMF UEFI 固件（从 Debian `qemu-efi-aarch64` 包解出）
- ROS 2 Jazzy（guest 内 apt 安装）
