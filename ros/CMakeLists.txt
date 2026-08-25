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

add_executable(npu_infer_node src/npu_infer_node.cpp)
target_include_directories(npu_infer_node PRIVATE include)
ament_target_dependencies(npu_infer_node rclcpp)
rosidl_target_interfaces(npu_infer_node ${PROJECT_NAME} "rosidl_typesupport_cpp")

install(TARGETS npu_infer_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
