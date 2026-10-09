cmake_minimum_required(VERSION 3.22)
project(spwcam_tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link against the spwcam_sdk target from the parent CMake project.
if(NOT TARGET spwcam_sdk)
    message(WARNING "spwcam_sdk target not found; tests skipped.")
    return()
endif()

set(INC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

function(spwcam_add_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${INC})
    target_link_libraries(${name} PRIVATE spwcam_sdk)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    # Copy DLL next to test executables so they can find it at runtime
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:spwcam_sdk> $<TARGET_FILE_DIR:${name}>)
endfunction()

# ── ABI smoke test (pure C, no runtime required) ─────────────────────────────
spwcam_add_test(test_abi test_abi.c)
add_test(NAME abi_smoke COMMAND test_abi)

# ── Init / deinit / multi-context / re-entrancy (no camera required) ─────────
spwcam_add_test(test_init test_init.cpp)
add_test(NAME init_deinit COMMAND test_init)

# ── End-to-end stream / record / snapshot test ───────────────────────────────
# Needs a live H.264 RTSP source: set SPWCAM_TEST_RTSP_URL (e.g. a camera or a
# local gst-rtsp-server).  Without it the test prints SKIP and passes.
spwcam_add_test(test_stream test_stream.cpp)
add_test(NAME stream_e2e COMMAND test_stream)
set_tests_properties(stream_e2e PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "SPWCAM_TEST_OUT_DIR=${CMAKE_BINARY_DIR}/test_out")
