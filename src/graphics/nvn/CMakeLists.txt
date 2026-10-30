cmake_minimum_required(VERSION 3.22)
project(ffrs_nvn_boundary LANGUAGES C)

find_package(Threads REQUIRED)
add_library(ffrs_nvn_dispatch STATIC src/nvn_dispatch.c)
target_include_directories(ffrs_nvn_dispatch PUBLIC include)
target_compile_features(ffrs_nvn_dispatch PUBLIC c_std_11)
target_compile_definitions(ffrs_nvn_dispatch PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(ffrs_nvn_dispatch PUBLIC Threads::Threads)

# This target supplies an API boundary only. Linking it does not install a
# Vulkan backend or demonstrate that a single frame can be rendered.
