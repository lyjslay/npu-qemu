cmake_minimum_required(VERSION 3.8)
project(npu_ros)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/NpuResult.msg"
)

# 订阅者：两端都构建（不依赖 /dev/npu）
add_executable(npu_subscriber src/npu_subscriber.cpp)
ament_target_dependencies(npu_subscriber rclcpp)
rosidl_target_interfaces(npu_subscriber ${PROJECT_NAME} "rosidl_typesupport_cpp")

# macOS 修复：rclcpp 传递依赖 libpython（_PyExc_* 等符号）需显式链接；
# 并设置 rpath 指向 conda env 的 lib，避免 librclcpp 找不到
if(APPLE)
  find_package(Python3 REQUIRED COMPONENTS Development)
  target_link_libraries(npu_subscriber Python3::Python)
  set_target_properties(npu_subscriber PROPERTIES
    BUILD_RPATH "$ENV{CONDA_PREFIX}/lib"
    INSTALL_RPATH "$ENV{CONDA_PREFIX}/lib")
endif()

install(TARGETS npu_subscriber
  DESTINATION lib/${PROJECT_NAME})

# 发布者节点：只在 Linux 上构建（依赖 /dev/npu 与 linux/ioctl.h）
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(npu_infer_node src/npu_infer_node.cpp)
  target_include_directories(npu_infer_node PRIVATE include)
  ament_target_dependencies(npu_infer_node rclcpp)
  rosidl_target_interfaces(npu_infer_node ${PROJECT_NAME} "rosidl_typesupport_cpp")
  install(TARGETS npu_infer_node
    DESTINATION lib/${PROJECT_NAME})
endif()

# Python 订阅者脚本
install(PROGRAMS scripts/npu_subscriber.py
  DESTINATION lib/${PROJECT_NAME})

ament_package()
