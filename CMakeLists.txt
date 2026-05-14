cmake_minimum_required(VERSION 3.10)
project(InfraredTempExtractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_TESSERACT "Enable Tesseract OCR support (optional)" OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)

add_executable(infrared_temp_extractor src/infrared_temp_extractor.cpp)
target_include_directories(infrared_temp_extractor PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(infrared_temp_extractor PRIVATE ${OpenCV_LIBS})

if(USE_TESSERACT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TESSERACT REQUIRED tesseract)
    pkg_check_modules(LEPTONICA REQUIRED lept)

    target_compile_definitions(infrared_temp_extractor PRIVATE USE_TESSERACT)
    target_include_directories(infrared_temp_extractor PRIVATE
        ${TESSERACT_INCLUDE_DIRS}
        ${LEPTONICA_INCLUDE_DIRS}
    )
    target_link_libraries(infrared_temp_extractor PRIVATE
        ${TESSERACT_LIBRARIES}
        ${LEPTONICA_LIBRARIES}
    )
    target_link_directories(infrared_temp_extractor PRIVATE
        ${TESSERACT_LIBRARY_DIRS}
        ${LEPTONICA_LIBRARY_DIRS}
    )
    message(STATUS "Tesseract OCR support: ENABLED")
else()
    message(STATUS "Tesseract OCR support: DISABLED (built-in digit recognizer only)")
endif()

# Install
install(TARGETS infrared_temp_extractor DESTINATION bin)
