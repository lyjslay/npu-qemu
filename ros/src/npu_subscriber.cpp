/*
 * C++ 订阅者：在 macOS 端订阅 QEMU guest NPU 节点发布的 /npu/result。
 * 收到消息后在回调里加业务逻辑即可。
 */
#include <rclcpp/rclcpp.hpp>
#include "npu_ros/msg/npu_result.hpp"
#include <functional>

using npu_ros::msg::NpuResult;

class NpuSubscriber : public rclcpp::Node
{
public:
    NpuSubscriber() : Node("npu_subscriber")
    {
        sub_ = this->create_subscription<NpuResult>(
            "/npu/result", 10,
            std::bind(&NpuSubscriber::on_result, this, std::placeholders::_1));
        RCLCPP_INFO(this->get_logger(), "订阅者已启动，监听 /npu/result");
    }

private:
    /* 收到 NPU 推理结果时的回调：在这里加你的业务逻辑 */
    void on_result(const NpuResult::SharedPtr msg)
    {
        RCLCPP_INFO(this->get_logger(),
            "收到推理结果: class=%d label=%s scores=[%d,%d,%d] input=[%d,%d,%d,%d]",
            msg->class_id, msg->label.c_str(),
            msg->scores[0], msg->scores[1], msg->scores[2],
            msg->input[0], msg->input[1], msg->input[2], msg->input[3]);

        // TODO: 在这里根据 msg 做后续处理，例如：
        //   if (msg->class_id == 2) { RCLCPP_WARN(...); /* 触发告警 */ }
        //   转发到别的 topic / 写日志 / 调外部服务 ...
    }

    rclcpp::Subscription<NpuResult>::SharedPtr sub_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<NpuSubscriber>());
    rclcpp::shutdown();
    return 0;
}
