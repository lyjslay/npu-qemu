#!/bin/bash
# 用 vmnet-shared 启动 NPU QEMU guest（需要 sudo：vmnet 接口创建需 root 权限）
# 用法：sudo ./run-vmnet.sh
#
# 为什么用 sudo：vmnet 的 com.apple.security.vmnet 是「受限 entitlement」
# （需付费 Apple 开发者账号 + Apple 审批才能用），ad-hoc 签名无效，
# 因此用 sudo 让 QEMU 以 root 运行来获得创建 vmnet 接口的权限。
#
# 启动后 guest 经 DHCP 拿到 IP（如 192.168.2.2），macOS 在 192.168.2.1。
set -e

NPU_UBUNTU="${NPU_UBUNTU:-$HOME/npu-ubuntu}"
QEMU="${QEMU:-$HOME/qemu/build/qemu-system-aarch64}"

pkill -f qemu-system-aarch64 2>/dev/null || true
sleep 1

cd "$NPU_UBUNTU"
rm -f console.log qemu_stderr.log qemu_stdout.log

nohup "$QEMU" \
  -M virt -cpu cortex-a72 -smp 4 -m 4G \
  -drive if=pflash,format=raw,readonly=on,file="$NPU_UBUNTU/AAVMF_CODE.fd" \
  -drive if=pflash,format=raw,file="$NPU_UBUNTU/AAVMF_VARS.fd" \
  -drive file="$NPU_UBUNTU/ubuntu.img",if=none,id=hd0,format=qcow2 \
  -device virtio-blk-device,drive=hd0 \
  -netdev vmnet-shared,id=net0 \
  -device virtio-net-device,netdev=net0 \
  -device npu \
  -serial file:"$NPU_UBUNTU/console.log" \
  -monitor unix:"$NPU_UBUNTU/mon.sock",server,nowait \
  -display none \
  > "$NPU_UBUNTU/qemu_stdout.log" 2> "$NPU_UBUNTU/qemu_stderr.log" &

echo "QEMU 已后台启动（vmnet-shared）。"
echo "guest 约 1 分钟内拿到 DHCP IP（如 192.168.2.2）："
echo "  ssh -i ~/.ssh/npu_qemu ubuntu@<guest-ip>"
