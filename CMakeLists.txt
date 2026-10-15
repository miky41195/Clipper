cmake_minimum_required(VERSION 3.28)
project(Clipper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
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
FetchContent_MakeAvailable(doctest json)

file(GLOB_RECURSE CLIPPER_CORE_SRC CONFIGURE_DEPENDS src/*.cpp)
list(FILTER CLIPPER_CORE_SRC EXCLUDE REGEX ".*/src/app/main\\.cpp$")
add_library(clipper_core STATIC ${CLIPPER_CORE_SRC})
target_include_directories(clipper_core PUBLIC src)
target_link_libraries(clipper_core PUBLIC
  nlohmann_json::nlohmann_json
  windowsapp d3d11 dxgi d2d1 dwrite mfplat mfuuid mf wmcodecdspuuid strmiids
  mmdevapi winmm version shell32 ole32 oleaut32 user32 gdi32 propsys)

add_executable(clipper WIN32 src/app/main.cpp)
target_link_libraries(clipper PRIVATE clipper_core)

file(GLOB CLIPPER_TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
add_executable(clipper_tests ${CLIPPER_TEST_SRC})
target_include_directories(clipper_tests PRIVATE tests)
target_link_libraries(clipper_tests PRIVATE clipper_core doctest::doctest)

enable_testing()
add_test(NAME unit COMMAND clipper_tests -tse=gpu,capture,audio-device,desktop)
