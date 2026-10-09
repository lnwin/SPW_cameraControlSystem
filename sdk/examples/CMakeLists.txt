cmake_minimum_required(VERSION 3.22)
project(spwcam_examples CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Built as part of the SDK tree: link the spwcam_sdk target directly so the
# examples are always configured and built (the import lib is a build output
# and does not exist at configure time).  When building the examples from the
# shipped dist package, point SPWCAM_DIST at the unpacked SDK directory.
set(SPWCAM_DIST "" CACHE PATH "Path to an unpacked spwcam_sdk dist (for out-of-tree builds)")

if(TARGET spwcam_sdk)
    set(SPWCAM_LINK spwcam_sdk)
    set(SPWCAM_INC  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
elseif(SPWCAM_DIST)
    add_library(spwcam_sdk_import SHARED IMPORTED)
    set_target_properties(spwcam_sdk_import PROPERTIES
        IMPORTED_LOCATION "${SPWCAM_DIST}/bin/spwcam_sdk.dll"
        IMPORTED_IMPLIB   "${SPWCAM_DIST}/lib/spwcam_sdk.lib")
    set(SPWCAM_LINK spwcam_sdk_import)
    set(SPWCAM_INC  ${SPWCAM_DIST}/include)
else()
    message(WARNING "spwcam_sdk target not found and SPWCAM_DIST not set; examples skipped.")
    return()
endif()

foreach(ex IN ITEMS example_c_basic example_cpp_raii)
    if(ex STREQUAL "example_c_basic")
        add_executable(${ex} c/example_basic.c)
    else()
        add_executable(${ex} cpp/example_raii.cpp)
    endif()
    target_include_directories(${ex} PRIVATE ${SPWCAM_INC})
    target_link_libraries(${ex} PRIVATE ${SPWCAM_LINK})
    if(MSVC)
        target_compile_options(${ex} PRIVATE /utf-8)
        target_compile_definitions(${ex} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
