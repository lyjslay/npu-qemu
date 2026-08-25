/*
 * ROS 2 节点 npu_infer_node：
 *   定时读取样本 -> 调 /dev/npu 做推理 -> 发布 NpuResult 到 /npu/result
 */
#include <rclcpp/rclcpp.hpp>
#include "npu_ros/msg/npu_result.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

#include "npu.h"
#include "sensor_model.h"

using namespace std::chrono_literals;

class NpuInferNode : public rclcpp::Node
{
public:
    NpuInferNode()
    : Node("npu_infer_node"), sample_idx_(0)
    {
        pub_ = this->create_publisher<npu_ros::msg::NpuResult>("/npu/result", 10);
        timer_ = this->create_wall_timer(2s, std::bind(&NpuInferNode::timer_cb, this));

        fd_ = open("/dev/npu", O_RDWR);
        if (fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "无法打开 /dev/npu (驱动加载了吗?)");
        } else {
            RCLCPP_INFO(this->get_logger(), "NPU 设备已打开");
        }
    }

    ~NpuInferNode()
    {
        if (fd_ >= 0) close(fd_);
    }

private:
    void timer_cb()
    {
        if (fd_ < 0) return;

        /* 三个样本轮流：正常/警告/故障 */
        int32_t samples[3][N_INPUTS] = {
            { 500,   800, 40, 3300 },
            { 800,   900, 40, 3300 },
            { 1200, 1000, 45, 3300 },
        };
        int32_t *input = samples[sample_idx_];

        int32_t output[N_CLASSES];
        struct npu_infer_args args;
        std::memset(&args, 0, sizeof(args));
        args.input = input;
        args.output = output;
        args.weights = weights;
        args.bias = bias;
        args.n_inputs = N_INPUTS;
        args.n_classes = N_CLASSES;

        if (ioctl(fd_, NPU_IOCTL_INFER, &args) < 0) {
            RCLCPP_ERROR(this->get_logger(), "ioctl 推理失败");
            return;
        }

        auto msg = npu_ros::msg::NpuResult();
        msg.class_id = args.result;
        msg.label = class_names[args.result];
        msg.scores = { output[0], output[1], output[2] };
        msg.input = { input[0], input[1], input[2], input[3] };

        pub_->publish(msg);
        RCLCPP_INFO(this->get_logger(), "推理: class=%s scores=[%d,%d,%d]",
                    msg.label.c_str(), output[0], output[1], output[2]);

        sample_idx_ = (sample_idx_ + 1) % 3;
    }

    rclcpp::Publisher<npu_ros::msg::NpuResult>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    int fd_;
    int sample_idx_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<NpuInferNode>());
    rclcpp::shutdown();
    return 0;
}
