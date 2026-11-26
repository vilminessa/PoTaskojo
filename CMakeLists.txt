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

# ---- ImPlot (charts: axes, legends, hover tooltips; MIT, vendored) ----
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/external/implot)
add_library(implot STATIC
  ${IMPLOT_DIR}/implot.cpp
  ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

# ---- App ----
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(potaskojo WIN32 ${APP_SOURCES})
target_include_directories(potaskojo PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(potaskojo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# The version the program reports comes from the same file the release gate
# checks against the tag, so the two cannot drift apart.
file(READ ${CMAKE_SOURCE_DIR}/VERSION POTASKOJO_VERSION_TEXT)
string(STRIP "${POTASKOJO_VERSION_TEXT}" POTASKOJO_VERSION_TEXT)
target_compile_definitions(potaskojo PRIVATE
  POTASKOJO_VERSION="${POTASKOJO_VERSION_TEXT}")
target_link_libraries(potaskojo PRIVATE
  imgui
  implot
  d3d11 dxgi d2d1 dwrite dwmapi
  pdh iphlpapi advapi32 shell32 ole32 comctl32 version
  ws2_32 bcrypt crypt32 wintrust winhttp
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

# ---- Tests ----
# There is no library target to link against: the app is one glob of sources
# compiled straight into an executable. The test binary therefore compiles the
# units under test directly - pulling the whole app in would drag D3D11 and the
# window along for the ride.
enable_testing()
add_executable(potaskojo_tests
  tests/main.cpp
  tests/test_format.cpp
  tests/test_settings.cpp
  tests/test_scale.cpp
  tests/test_rule_store.cpp
  tests/test_watchdog.cpp
  tests/test_action_exec.cpp
  tests/test_affinity.cpp
  tests/test_rule_engine.cpp
  tests/test_event_log.cpp
  tests/test_text.cpp
  tests/test_update.cpp
  tests/test_chart_time.cpp
  src/core/format.cpp
  src/core/settings.cpp
  src/core/paths.cpp
  src/core/text.cpp
  src/core/log.cpp
  src/ui/scale.cpp
  src/ui/chart_time.cpp
  src/rules/rule_store.cpp
  src/rules/watchdog_eval.cpp
  src/rules/action_exec.cpp
  src/rules/cpu_affinity.cpp
  src/rules/rule_engine.cpp
  src/rules/event_log.cpp
  src/platform/crash_handler.cpp
  src/platform/tray.cpp
  src/update/update_format.cpp
  src/update/update_crypto.cpp
  src/update/update_check.cpp
  src/update/update_install.cpp
  src/update/update_schedule.cpp
)
target_include_directories(potaskojo_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
)
target_compile_definitions(potaskojo_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(potaskojo_tests PRIVATE shell32 ole32 bcrypt winhttp)
if(MSVC)
  target_compile_options(potaskojo_tests PRIVATE
    /W4 /permissive- /Zc:__cplusplus /utf-8)
  if(POTASKOJO_WERROR)
    target_compile_options(potaskojo_tests PRIVATE /WX)
  endif()
endif()
add_test(NAME unit COMMAND potaskojo_tests)
# Keep the test binary out of the app's own directory: it creates a
# PoTaskojoData folder next to itself to isolate its file I/O, and that same
# folder would flip potaskojo.exe into portable mode and silently move the
# developer's settings out of the profile.
set_target_properties(potaskojo_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
