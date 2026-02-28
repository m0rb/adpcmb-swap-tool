cmake_minimum_required(VERSION 3.10)
project(adpcmb-swap-tool VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# miniz library (for ZIP support)
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)

# Main executable
add_executable(adpcmb-swap
    src/main.cpp
    src/z80_dasm.cpp
    src/scanner.cpp
    src/patcher.cpp
    src/romio.cpp
)
target_link_libraries(adpcmb-swap PRIVATE miniz)
target_include_directories(adpcmb-swap PRIVATE src)
target_compile_definitions(adpcmb-swap PRIVATE
    APP_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(adpcmb-swap PRIVATE /W4)
else()
    target_compile_options(adpcmb-swap PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS adpcmb-swap RUNTIME DESTINATION bin)
