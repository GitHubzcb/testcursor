cmake_minimum_required(VERSION 3.10)
project(NeedleBendDetection LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)
include_directories(${OpenCV_INCLUDE_DIRS})

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")

add_executable(needle_bend_detect src/main.cpp)
target_link_libraries(needle_bend_detect ${OpenCV_LIBS})
