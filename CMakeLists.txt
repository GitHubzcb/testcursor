cmake_minimum_required(VERSION 3.12)
project(ir_human_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

add_executable(ir_human_detector src/main.cpp)
target_link_libraries(ir_human_detector PRIVATE ${OpenCV_LIBS})

if(MSVC)
  target_compile_options(ir_human_detector PRIVATE /W4)
else()
  target_compile_options(ir_human_detector PRIVATE -Wall -Wextra -Wpedantic)
endif()
