cmake_minimum_required(VERSION 3.22)
project(phantom CXX)
set(CMAKE_CXX_STANDARD 17)

set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui)

add_library(imgui STATIC
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_opengl3.cpp)

target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

set(DOBBY_DEBUG         OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST    OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/Dobby dobby_build)

add_library(phantom SHARED todo.cpp)
target_link_libraries(phantom imgui dobby log android EGL GLESv3)
target_compile_options(phantom PRIVATE -O2 -fvisibility=hidden)
