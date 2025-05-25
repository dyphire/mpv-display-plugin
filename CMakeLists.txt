cmake_minimum_required(VERSION 3.13)

project(mpv-display-plugin)
set(CMAKE_C_STANDARD 11)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
add_library(display-info SHARED src/display.c)
set_property(TARGET display-info PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(display-info
    PRIVATE
        dxgi
        dxguid
)

target_include_directories(display-info PRIVATE ${MPV_INCLUDE_DIRS})
target_compile_definitions(display-info PRIVATE MPV_CPLUGIN_DYNAMIC_SYM)
