/*
 * NPU 加速器 —— QEMU PCIe 设备模型（骨架）。
 *
 * 薄壳职责：MMIO 寄存器、命令队列、DMA、MSI 中断。
 * 指令语义在 npu-model.c 里实现。
 *
 * 用法：qemu-system-aarch64 -M virt -device npu
 *
 * 寄存器（BAR0，全部 32-bit 对齐）：
 *   +0x00 ID       RO  0x4e505531 ("NPU1")
 *   +0x04 STATUS   RO  功能模型状态 (NPU_STAT_*)
 *   +0x08 RESULT   RO  最近一次命令结果
 *   +0x10 CMDQ     RW  命令描述符的 guest 物理地址
 *   +0x14 DOORBELL WO  写任意值触发执行 CMDQ 指向的命令，完成后发 MSI
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "hw/core/qdev.h"
#include "npu-model.h"

#define TYPE_NPU "npu"
OBJECT_DECLARE_SIMPLE_TYPE(NPUState, NPU)

#define NPU_MMIO_SIZE   0x1000
#define NPU_PCI_VENDOR  0x1234
#define NPU_PCI_DEVICE  0x0001

/* BAR0 寄存器偏移 */
enum {
    NPU_REG_ID       = 0x00,
    NPU_REG_STATUS   = 0x04,
    NPU_REG_RESULT   = 0x08,
    NPU_REG_CMDQ     = 0x10,
    NPU_REG_DOORBELL = 0x14,
};

struct NPUState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    NPUModel model;
    uint64_t cmdq_gpa;
};

/* ---- DMA 回调：把功能模型接到 QEMU 的 address space ---- */
static int npu_dma_read(void *ctx, uint64_t gpa, void *buf, uint32_t len)
{
    MemTxResult r = address_space_read(&address_space_memory, gpa,
                                       MEMTXATTRS_UNSPECIFIED, buf, len);
    return r == MEMTX_OK ? 0 : -1;
}

static int npu_dma_write(void *ctx, uint64_t gpa, const void *buf, uint32_t len)
{
    MemTxResult r = address_space_write(&address_space_memory, gpa,
                                        MEMTXATTRS_UNSPECIFIED, buf, len);
    return r == MEMTX_OK ? 0 : -1;
}

/* ---- 执行：读命令描述符 -> 功能模型 -> 回结果 + 发中断 ---- */
static void npu_execute(NPUState *s)
{
    NPUCommand cmd;

    if (address_space_read(&address_space_memory, s->cmdq_gpa,
                           MEMTXATTRS_UNSPECIFIED, &cmd, sizeof(cmd)) != MEMTX_OK) {
        s->model.status = NPU_STAT_ERR;
        return;
    }

    npu_model_exec(&s->model, &cmd, s, npu_dma_read, npu_dma_write);

    fprintf(stderr, "NPU_DEBUG: msi_enabled=%d result=%u\n",
            msi_enabled(&s->parent_obj), s->model.result);
    if (msi_enabled(&s->parent_obj)) {
        msi_notify(&s->parent_obj, 0);
        fprintf(stderr, "NPU_DEBUG: msi_notify sent\n");
    }
}

/* ---- MMIO ---- */
static uint64_t npu_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    NPUState *s = opaque;
    switch (addr) {
    case NPU_REG_ID:     return 0x4e505531;
    case NPU_REG_STATUS: return s->model.status;
    case NPU_REG_RESULT: return s->model.result;
    default:             return 0;
    }
}

static void npu_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    NPUState *s = opaque;
    switch (addr) {
    case NPU_REG_CMDQ:
        s->cmdq_gpa = val;
        break;
    case NPU_REG_DOORBELL:
        npu_execute(s);
        break;
    }
}

static const MemoryRegionOps npu_mmio_ops = {
    .read = npu_mmio_read,
    .write = npu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void npu_realize(PCIDevice *pdev, Error **errp)
{
    NPUState *s = NPU(pdev);

    npu_model_init(&s->model);

    memory_region_init_io(&s->mmio, OBJECT(s), &npu_mmio_ops, s,
                          "npu-mmio", NPU_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    if (msi_init(pdev, 0, 1, true, false, NULL)) {
        error_setg(errp, "npu: failed to init MSI");
        return;
    }
}

static void npu_exit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void npu_reset(DeviceState *dev)
{
    NPUState *s = NPU(dev);
    npu_model_init(&s->model);
    s->cmdq_gpa = 0;
}

static void npu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = npu_realize;
    pc->exit = npu_exit;
    pc->vendor_id = NPU_PCI_VENDOR;
    pc->device_id = NPU_PCI_DEVICE;
    pc->revision = 0x01;
    pc->class_id = PCI_CLASS_OTHERS;
    device_class_set_legacy_reset(dc, npu_reset);
    dc->desc = "NPU accelerator (functional model skeleton)";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo npu_info = {
    .name = TYPE_NPU,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NPUState),
    .class_init = npu_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void npu_register_types(void)
{
    type_register_static(&npu_info);
}
type_init(npu_register_types)
