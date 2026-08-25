#!/usr/bin/env python3
"""Python 订阅者：在 macOS 端订阅 QEMU guest NPU 节点发布的 /npu/result。"""
import rclpy
from rclpy.node import Node
from npu_ros.msg import NpuResult


class NpuSubscriber(Node):
    def __init__(self):
        super().__init__('npu_subscriber')
        self.subscription = self.create_subscription(
            NpuResult, '/npu/result', self.on_result, 10)
        self.get_logger().info('订阅者已启动，监听 /npu/result')

    def on_result(self, msg: NpuResult):
        """收到推理结果时的回调：在这里加你的业务逻辑。"""
        self.get_logger().info(
            f'收到推理结果: class={msg.class_id} label={msg.label} '
            f'scores={list(msg.scores)} input={list(msg.input)}')

        # TODO: 在这里根据 msg 做后续处理，例如：
        #   if msg.class_id == 2:
        #       self.get_logger().warn('检测到 critical，触发告警！')


def main():
    rclpy.init()
    node = NpuSubscriber()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
