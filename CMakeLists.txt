cmake_minimum_required(VERSION 3.25)
project(potaskojo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---- Dear ImGui (docking) ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# ---- App ----
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(potaskojo WIN32 ${APP_SOURCES})
target_include_directories(potaskojo PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(potaskojo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(potaskojo PRIVATE
  imgui
  d3d11 dxgi d2d1 dwrite dwmapi
  pdh iphlpapi advapi32 shell32 ole32 comctl32 version
  ws2_32 bcrypt crypt32 wintrust
)
if(MSVC)
  target_compile_options(potaskojo PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
  target_compile_options(imgui PRIVATE /w)
endif()

# CI gate: treat our warnings as errors (imgui stays exempt above).
option(POTASKOJO_WERROR "Treat compiler warnings as errors" OFF)
if(MSVC AND POTASKOJO_WERROR)
  target_compile_options(potaskojo PRIVATE /WX)
endif()
