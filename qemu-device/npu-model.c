/*
 * NPU 功能模型（骨架）实现。
 * 纯 C，无 QEMU 依赖，可独立编译单测。
 */
#include "npu-model.h"

void npu_model_init(NPUModel *m)
{
    m->status = NPU_STAT_IDLE;
    m->result = 0;
    m->cmd_count = 0;
}

/* 分块 DMA 搬运：src -> dst，nwords 个 u32 */
static int npu_dma_copy(void *ctx, NPUReadFn rd, NPUWriteFn wr,
                        uint64_t dst, uint64_t src, uint32_t nwords)
{
    uint32_t buf[64];
    uint32_t off = 0;

    while (off < nwords) {
        uint32_t chunk = nwords - off;
        if (chunk > 64) {
            chunk = 64;
        }
        if (rd(ctx, src + off * 4, buf, chunk * 4) != 0) {
            return -1;
        }
        if (wr(ctx, dst + off * 4, buf, chunk * 4) != 0) {
            return -1;
        }
        off += chunk;
    }
    return 0;
}

int npu_model_exec(NPUModel *m, const NPUCommand *cmd,
                   void *ctx, NPUReadFn rd, NPUWriteFn wr)
{
    m->status = NPU_STAT_BUSY;
    m->cmd_count++;

    switch (cmd->opcode) {
    case NPU_OP_NOP:
        m->result = 0;
        break;
    case NPU_OP_ADD:
        m->result = cmd->a + cmd->b;
        break;
    case NPU_OP_MUL:
        m->result = cmd->a * cmd->b;
        break;
    case NPU_OP_COPY:
        if (npu_dma_copy(ctx, rd, wr, cmd->dst, cmd->src, cmd->count) != 0) {
            m->status = NPU_STAT_ERR;
            return -1;
        }
        m->result = cmd->count;
        break;

    case NPU_OP_INFER: {
        uint32_t n = cmd->a, m_cnt = cmd->b;
        int32_t in[NPU_MAX_DIM], w[NPU_MAX_DIM];
        int32_t bias[NPU_MAX_DIM], out[NPU_MAX_DIM];
        uint32_t i, j;
        int best = 0;

        if (n == 0 || m_cnt == 0 || n > NPU_MAX_DIM || m_cnt > NPU_MAX_DIM) {
            m->status = NPU_STAT_ERR;
            return -1;
        }
        if (rd(ctx, cmd->src, in, n * 4) != 0 ||
            rd(ctx, cmd->bias, bias, m_cnt * 4) != 0) {
            m->status = NPU_STAT_ERR;
            return -1;
        }

        /* out[j] = bias[j] + Σ_i in[i] * W[j][i]，行主序 W[j][i] = weights[j*n+i] */
        for (j = 0; j < m_cnt; j++) {
            int64_t acc = bias[j];
            if (rd(ctx, cmd->weights + (uint64_t)j * n * 4, w, n * 4) != 0) {
                m->status = NPU_STAT_ERR;
                return -1;
            }
            for (i = 0; i < n; i++) {
                acc += (int64_t)in[i] * w[i];
            }
            out[j] = (int32_t)acc;
            if (out[j] > out[best]) {
                best = (int)j;
            }
        }

        if (wr(ctx, cmd->dst, out, m_cnt * 4) != 0) {
            m->status = NPU_STAT_ERR;
            return -1;
        }
        m->result = (uint32_t)best;
        break;
    }

    default:
        m->status = NPU_STAT_ERR;
        return -1;
    }

    m->status = NPU_STAT_DONE;
    return 0;
}
