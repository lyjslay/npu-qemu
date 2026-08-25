/*
 * NPU 驱动（PCI，完整版）—— 演示真实软件链路：
 *   使能设备 -> 32-bit DMA mask -> iomap BAR -> 分配 MSI -> 注册中断
 *   -> 分配 DMA buffer -> 注册 misc 设备 /dev/npu -> ioctl 下发推理
 *   -> 写命令队列 + doorbell -> 等 MSI 中断 -> 读结果
 *
 * 编译（guest 内，需内核头文件）：
 *   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "npu.h"

#define PCI_VENDOR_NPU 0x1234
#define PCI_DEVICE_NPU 0x0001

/* 与设备模型一致的寄存器偏移 */
enum { REG_ID = 0x00, REG_STATUS = 0x04, REG_RESULT = 0x08,
       REG_CMDQ = 0x10, REG_DOORBELL = 0x14 };
#define OP_INFER  0x04
#define STAT_DONE 0x2

/* 与设备功能模型一致的命令描述符（56 字节，布局必须严格匹配） */
struct npu_cmd {
    u32 opcode, flags;
    u64 src, dst;
    u32 a, b, count, reserved;
    u64 weights, bias;
};

struct npu_dev {
    struct pci_dev *pdev;
    void __iomem *bar;
    int irq;
    struct completion done;
    struct miscdevice misc;

    /* DMA buffer（按 NPU_MAX_DIM 上限一次分配，ioctl 复用） */
    void *input;   dma_addr_t input_dma;
    void *output;  dma_addr_t output_dma;
    void *weights; dma_addr_t weights_dma;
    void *bias;    dma_addr_t bias_dma;
    struct npu_cmd *cmd; dma_addr_t cmd_dma;
};

static struct npu_dev *g_npu;

static irqreturn_t npu_isr(int irq, void *data)
{
    struct npu_dev *n = data;
    pr_info("npu: MSI received (irq=%d)\n", irq);
    complete(&n->done);
    return IRQ_HANDLED;
}

/* 下发命令并等待 MSI 完成 */
static int npu_submit_infer(struct npu_dev *n, u32 n_in, u32 m_cls)
{
    memset(n->cmd, 0, sizeof(*n->cmd));
    n->cmd->opcode  = OP_INFER;
    n->cmd->src     = n->input_dma;
    n->cmd->dst     = n->output_dma;
    n->cmd->weights = n->weights_dma;
    n->cmd->bias    = n->bias_dma;
    n->cmd->a       = n_in;
    n->cmd->b       = m_cls;

    iowrite32((u32)n->cmd_dma, n->bar + REG_CMDQ);   /* 描述符物理地址 */
    iowrite32(1, n->bar + REG_DOORBELL);             /* 触发执行 */

    /* 轮询状态（QEMU 下 MSI 路由暂不可用，用轮询兜底；真实硬件可改用中断） */
    {
        int loops;
        for (loops = 0; loops < 1000000; loops++) {
            if (ioread32(n->bar + REG_STATUS) == STAT_DONE)
                return 0;
            cpu_relax();
        }
        pr_err("npu: timeout! status=0x%x\n", ioread32(n->bar + REG_STATUS));
        return -ETIMEDOUT;
    }
    return 0;
}

static long npu_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct npu_dev *n = g_npu;
    struct npu_infer_args args;
    u32 n_in, m_cls;
    int ret;

    if (cmd != NPU_IOCTL_INFER || !n)
        return -EINVAL;

    if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
        return -EFAULT;

    n_in = args.n_inputs;
    m_cls = args.n_classes;
    if (n_in == 0 || m_cls == 0 || n_in > NPU_MAX_DIM || m_cls > NPU_MAX_DIM)
        return -EINVAL;

    if (copy_from_user(n->input, args.input, n_in * 4) ||
        copy_from_user(n->weights, args.weights, n_in * m_cls * 4) ||
        copy_from_user(n->bias, args.bias, m_cls * 4))
        return -EFAULT;

    ret = npu_submit_infer(n, n_in, m_cls);
    if (ret)
        return ret;

    if (copy_to_user(args.output, n->output, m_cls * 4))
        return -EFAULT;

    args.result = (int32_t)ioread32(n->bar + REG_RESULT);
    if (copy_to_user((void __user *)arg, &args, sizeof(args)))
        return -EFAULT;

    return 0;
}

static const struct file_operations npu_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = npu_ioctl,
};

static void npu_free_dma(struct npu_dev *n)
{
    struct device *dev = &n->pdev->dev;
    if (n->input)   dma_free_coherent(dev, NPU_MAX_DIM * 4, n->input, n->input_dma);
    if (n->output)  dma_free_coherent(dev, NPU_MAX_DIM * 4, n->output, n->output_dma);
    if (n->weights) dma_free_coherent(dev, NPU_MAX_DIM * NPU_MAX_DIM * 4, n->weights, n->weights_dma);
    if (n->bias)    dma_free_coherent(dev, NPU_MAX_DIM * 4, n->bias, n->bias_dma);
    if (n->cmd)     dma_free_coherent(dev, sizeof(*n->cmd), n->cmd, n->cmd_dma);
}

static int npu_alloc_dma(struct npu_dev *n)
{
    struct device *dev = &n->pdev->dev;
    n->input   = dma_alloc_coherent(dev, NPU_MAX_DIM * 4, &n->input_dma, GFP_KERNEL);
    n->output  = dma_alloc_coherent(dev, NPU_MAX_DIM * 4, &n->output_dma, GFP_KERNEL);
    n->weights = dma_alloc_coherent(dev, NPU_MAX_DIM * NPU_MAX_DIM * 4, &n->weights_dma, GFP_KERNEL);
    n->bias    = dma_alloc_coherent(dev, NPU_MAX_DIM * 4, &n->bias_dma, GFP_KERNEL);
    n->cmd     = dma_alloc_coherent(dev, sizeof(*n->cmd), &n->cmd_dma, GFP_KERNEL);
    if (!n->input || !n->output || !n->weights || !n->bias || !n->cmd) {
        npu_free_dma(n);
        return -ENOMEM;
    }
    return 0;
}

static int npu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct npu_dev *n;
    int ret;

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    /* 设备只有 32-bit 命令指针寄存器，强制 32-bit DMA */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret)
        return ret;

    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (ret < 0)
        return ret;

    n = devm_kzalloc(&pdev->dev, sizeof(*n), GFP_KERNEL);
    if (!n)
        return -ENOMEM;
    n->pdev = pdev;
    n->bar = pcim_iomap(pdev, 0, 0);
    if (!n->bar)
        return -ENOMEM;
    n->irq = pci_irq_vector(pdev, 0);
    init_completion(&n->done);

    ret = npu_alloc_dma(n);
    if (ret)
        return ret;

    ret = request_irq(n->irq, npu_isr, 0, "npu", n);
    if (ret) {
        npu_free_dma(n);
        return ret;
    }

    n->misc.minor = MISC_DYNAMIC_MINOR;
    n->misc.name = "npu";
    n->misc.fops = &npu_fops;
    n->misc.mode = 0666;   /* 让普通用户（ROS 节点）也能 open /dev/npu */
    ret = misc_register(&n->misc);
    if (ret) {
        free_irq(n->irq, n);
        npu_free_dma(n);
        return ret;
    }

    pci_set_drvdata(pdev, n);
    g_npu = n;

    pr_info("npu: probed bar=%p id=0x%08x irq=%d\n",
            n->bar, ioread32(n->bar + REG_ID), n->irq);
    return 0;
}

static void npu_remove(struct pci_dev *pdev)
{
    struct npu_dev *n = pci_get_drvdata(pdev);
    if (!n)
        return;
    misc_deregister(&n->misc);
    free_irq(n->irq, n);
    npu_free_dma(n);
    pci_free_irq_vectors(pdev);
    g_npu = NULL;
}

static const struct pci_device_id npu_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_NPU, PCI_DEVICE_NPU) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, npu_ids);

static struct pci_driver npu_driver = {
    .name = "npu",
    .id_table = npu_ids,
    .probe = npu_probe,
    .remove = npu_remove,
};
module_pci_driver(npu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NPU accelerator driver (inference)");
MODULE_AUTHOR("you");
