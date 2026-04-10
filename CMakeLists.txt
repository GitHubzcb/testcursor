cmake_minimum_required(VERSION 3.10)
project(InfraredTempExtractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)

add_executable(infrared_temp_extractor src/infrared_temp_extractor.cpp)
target_link_libraries(infrared_temp_extractor PRIVATE ${OpenCV_LIBS})
target_include_directories(infrared_temp_extractor PRIVATE ${OpenCV_INCLUDE_DIRS})

# Install
install(TARGETS infrared_temp_extractor DESTINATION bin)
