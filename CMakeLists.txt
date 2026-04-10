cmake_minimum_required(VERSION 3.14)
project(TemperatureChart VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(tempcharts
    src/main.cpp
    src/csv_reader.cpp
    src/chart_generator.cpp
)

target_include_directories(tempcharts PRIVATE ${CMAKE_SOURCE_DIR}/include)

install(TARGETS tempcharts DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/tempcharts/data)
