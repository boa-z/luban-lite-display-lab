# Host checks for Luban-Lite Display Lab. The firmware is built by the SDK (SConscript); this file
# only compiles the portable parts and the LVGL-side tools against the SDK's own LVGL source.
cmake_minimum_required(VERSION 3.16)
project(luban_lite_display_lab C)
enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# The tests check with assert, so keep it in Release builds too.
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

add_executable(test-gamma tests/test_gamma.c src/gamma.c)
add_executable(test-ccm tests/test_ccm.c src/ccm.c)
foreach(test gamma ccm)
    target_include_directories(test-${test} PRIVATE src)
    if(UNIX)
        target_link_libraries(test-${test} PRIVATE m)
    endif()
    add_test(NAME ${test} COMMAND test-${test})
endforeach()

# The checkout sits at <sdk>/application/rt-thread/luban-lite-display-lab; elsewhere name the tree.
get_filename_component(sdk_lvgl "${CMAKE_CURRENT_SOURCE_DIR}/../../../packages/artinchip/lvgl-ui/lvgl_v9/lvgl" ABSOLUTE)
set(DISPLAY_LAB_LVGL_ROOT "${sdk_lvgl}" CACHE PATH "LVGL source tree (the SDK lvgl-ui v9 package)")
if(EXISTS "${DISPLAY_LAB_LVGL_ROOT}/lvgl.h")
    set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/lv_conf.h" CACHE FILEPATH "LVGL host configuration")
    foreach(flag LV_CONF_BUILD_DISABLE_DEMOS LV_CONF_BUILD_DISABLE_EXAMPLES LV_CONF_BUILD_DISABLE_THORVG_INTERNAL)
        set(${flag} ON CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${DISPLAY_LAB_LVGL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)

    add_executable(test-testcard tests/test_testcard.c src/testcard.c)
    target_include_directories(test-testcard PRIVATE src tests)
    target_link_libraries(test-testcard PRIVATE lvgl)
    add_test(NAME testcard COMMAND test-testcard)

    # display_bench.c against stub RT-Thread/MPP headers and the real LVGL.
    add_executable(test-display-bench tests/test_display_bench.c src/display_bench.c)
    target_include_directories(test-display-bench PRIVATE tests/stubs src)
    target_link_libraries(test-display-bench PRIVATE lvgl)
    add_test(NAME display-bench COMMAND test-display-bench)
else()
    message(STATUS "LVGL not found at ${DISPLAY_LAB_LVGL_ROOT}: testcard and display-bench checks skipped "
                   "(pass -DDISPLAY_LAB_LVGL_ROOT=<sdk>/packages/artinchip/lvgl-ui/lvgl_v9/lvgl)")
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME tuner COMMAND Python3::Interpreter -m unittest -v tests/test_tuner.py
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
