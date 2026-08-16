set(Proj_Name Stripe_Defect_Detect)

add_library(${Proj_Name}
    headers/stripe_defect_detect.h
    sources/stripe_defect_detect.cpp
)

target_include_directories(${Proj_Name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/headers>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(${Proj_Name} PUBLIC opencv_core PRIVATE opencv_imgproc)
target_compile_features(${Proj_Name} PUBLIC cxx_std_17)
target_compile_definitions(${Proj_Name} PRIVATE
    STRIPE_DEFECT_DETECT_EXPORTS
    Stripe_Defect_Detect_Debug=0
)

get_target_property(STRIPE_DEFECT_DETECT_LIBRARY_TYPE ${Proj_Name} TYPE)
if(STRIPE_DEFECT_DETECT_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(${Proj_Name} PUBLIC STRIPE_DEFECT_DETECT_STATIC)
endif()

set_target_properties(${Proj_Name} PROPERTIES
    DEBUG_POSTFIX "d"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_compile_options(${Proj_Name} PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

include(GNUInstallDirs)
install(TARGETS ${Proj_Name}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES headers/stripe_defect_detect.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
