cmake_minimum_required(VERSION 3.12)
project(cpp_ai_utils)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)

# 指定头文件路径
include_directories(
    /home/vanelord/bysj/opencv/include
    /home/vanelord/bysj/opencv/world_build
    /home/vanelord/bysj/opencv/modules/calib3d/include
    /home/vanelord/bysj/opencv/modules/core/include
    /home/vanelord/bysj/opencv/modules/dnn/include
    /home/vanelord/bysj/opencv/modules/features2d/include
    /home/vanelord/bysj/opencv/modules/flann/include
    /home/vanelord/bysj/opencv/modules/gapi/include
    /home/vanelord/bysj/opencv/modules/highgui/include
    /home/vanelord/bysj/opencv/modules/imgcodecs/include
    /home/vanelord/bysj/opencv/modules/imgproc/include
    /home/vanelord/bysj/opencv/modules/java/include
    /home/vanelord/bysj/opencv/modules/js/include
    /home/vanelord/bysj/opencv/modules/ml/include
    /home/vanelord/bysj/opencv/modules/objc/include
    /home/vanelord/bysj/opencv/modules/objdetect/include
    /home/vanelord/bysj/opencv/modules/photo/include
    /home/vanelord/bysj/opencv/modules/python/include
    /home/vanelord/bysj/opencv/modules/stitching/include
    /home/vanelord/bysj/opencv/modules/ts/include
    /home/vanelord/bysj/opencv/modules/video/include
    /home/vanelord/bysj/opencv/modules/videoio/include
    /home/vanelord/bysj/opencv/modules/world/include
    /home/vanelord/bysj/cpp_redis/includes
    /home/vanelord/bysj/cpp_redis/tacopie/includes
    /home/vanelord/bysj/spdlog/include
)

# 指定库路径
link_directories(
    /home/vanelord/bysj/opencv/world_build/lib
    /home/vanelord/bysj/cpp_redis/build/lib
)


# 添加你的库的源文件
add_library(cpp_ai_utils STATIC
    cpp_ai_utils.cpp
    pch.cpp
)

# 链接库
target_link_libraries(cpp_ai_utils
    libopencv_world.so
    libcpp_redis.so
    libtacopie.so
)
