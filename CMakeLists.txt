cmake_minimum_required(VERSION 3.18)

project(Stripe_Defect_Detect VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_subdirectory(projs/Stripe_Defect_Detect)

add_executable(stripe_detector
    main.cpp
    test/test_sdd.cpp
)
target_include_directories(stripe_detector PRIVATE test)
target_link_libraries(stripe_detector PRIVATE Stripe_Defect_Detect opencv_imgcodecs)
target_compile_options(stripe_detector PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

# Windows only searches the executable directory and PATH for dependent DLLs.
# Keep the detector DLL beside the CLI so CTest and direct execution can load it.
get_target_property(STRIPE_DEFECT_DETECT_TARGET_TYPE Stripe_Defect_Detect TYPE)
if(WIN32 AND STRIPE_DEFECT_DETECT_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET stripe_detector POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Stripe_Defect_Detect>"
            "$<TARGET_FILE_DIR:stripe_detector>"
        COMMENT "Copying Stripe_Defect_Detect DLL beside stripe_detector"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME stripe_defect_detect_tests COMMAND stripe_detector --self-test)
endif()
