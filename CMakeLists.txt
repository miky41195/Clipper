cmake_minimum_required(VERSION 3.28)
project(Clipper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# Static C++ runtime: friends' PCs need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /bigobj)
endif()
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
# Pinned month-end build (BtbN keeps those for ~2 years): reproducible builds, and the exact
# FFmpeg source the shipped LGPL DLLs come from (n8.1.3-9-g29e619e767) stays identifiable.
FetchContent_Declare(ffmpeg
  URL https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08/ffmpeg-n8.1.3-9-g29e619e767-win64-lgpl-shared-8.1.zip
  URL_HASH SHA256=3e47bda1607740550141e37c0e49d1e5182b34699f15adfd137ee266d346811a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doctest json ffmpeg imgui)

add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE ${ffmpeg_SOURCE_DIR}/include)
target_link_directories(ffmpeg INTERFACE ${ffmpeg_SOURCE_DIR}/lib)
target_link_libraries(ffmpeg INTERFACE avformat avcodec avutil swscale swresample)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(MSVC)
  target_compile_options(imgui PRIVATE /W0)
endif()
# Only the libraries Clipper links (the package also ships avfilter/avdevice, ~35 MB unused).
set(FFMPEG_DLLS)
foreach(lib avcodec avformat avutil swscale swresample)
  file(GLOB dll ${ffmpeg_SOURCE_DIR}/bin/${lib}-*.dll)
  list(APPEND FFMPEG_DLLS ${dll})
endforeach()
file(COPY ${FFMPEG_DLLS} DESTINATION ${CMAKE_BINARY_DIR}/bin)
file(REMOVE ${CMAKE_BINARY_DIR}/bin/avfilter-11.dll ${CMAKE_BINARY_DIR}/bin/avdevice-62.dll)

file(GLOB_RECURSE CLIPPER_CORE_SRC CONFIGURE_DEPENDS src/*.cpp)
list(FILTER CLIPPER_CORE_SRC EXCLUDE REGEX ".*/src/app/main\\.cpp$")
add_library(clipper_core STATIC ${CLIPPER_CORE_SRC})
target_include_directories(clipper_core PUBLIC src)
target_link_libraries(clipper_core PUBLIC
  nlohmann_json::nlohmann_json ffmpeg imgui
  windowsapp windowscodecs dwmapi d3dcompiler d3d11 dxgi d2d1 dwrite mfplat mfuuid mf wmcodecdspuuid strmiids
  mmdevapi winmm version shell32 shcore ole32 oleaut32 user32 gdi32 propsys psapi)

add_executable(clipper WIN32 src/app/main.cpp src/app/clipper.rc)
target_link_libraries(clipper PRIVATE clipper_core)

file(GLOB CLIPPER_TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
add_executable(clipper_tests ${CLIPPER_TEST_SRC})
target_include_directories(clipper_tests PRIVATE tests)
target_link_libraries(clipper_tests PRIVATE clipper_core doctest::doctest)
target_compile_definitions(clipper_tests PRIVATE CLIPPER_FFPROBE="${ffmpeg_SOURCE_DIR}/bin/ffprobe.exe")

enable_testing()
add_test(NAME unit COMMAND clipper_tests -tse=gpu,capture,audio-device,desktop)
