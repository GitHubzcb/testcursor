cmake_minimum_required(VERSION 3.14)
project(ThermalHumanDetector VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui)

# 静态库
add_library(thermal_human_detector STATIC
    src/thermal_human_detector.cpp
)
target_include_directories(thermal_human_detector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(thermal_human_detector PUBLIC
    ${OpenCV_LIBS}
)

# 示例程序
add_executable(thermal_detector_example
    example/main.cpp
)
target_link_libraries(thermal_detector_example PRIVATE
    thermal_human_detector
)
