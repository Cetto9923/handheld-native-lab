cmake_minimum_required(VERSION 3.22)
project(ffrs_android_diagnostics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PROBE_SOURCES probe_core.cpp probe_types.cpp probe_platform.cpp json_writer.cpp probe_vulkan.cpp)
if(ANDROID)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "The native NSO diagnostic requires arm64-v8a")
    endif()
    list(APPEND PROBE_SOURCES probe_android.cpp)
else()
    list(APPEND PROBE_SOURCES probe_host.cpp)
endif()
add_library(ffrs_probe STATIC ${PROBE_SOURCES})
target_include_directories(ffrs_probe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(ANDROID)
    target_link_libraries(ffrs_probe PUBLIC vulkan android log)
endif()
add_executable(probe_rg477m main.cpp)
target_link_libraries(probe_rg477m PRIVATE ffrs_probe)

if(ANDROID)
    # Read the one runtime implementation in place; never fork the loader here.
    set(FFRS_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime/nso" CACHE PATH "NSO loader source directory")
    add_library(ffrs_nso STATIC "${FFRS_RUNTIME_DIR}/nso_loader.c")
    target_include_directories(ffrs_nso PUBLIC "${FFRS_RUNTIME_DIR}")
    target_link_libraries(ffrs_nso PUBLIC dl)
    add_library(ffrs_diagnostic_core STATIC nso_diagnostic.cpp)
    target_link_libraries(ffrs_diagnostic_core PUBLIC ffrs_nso ffrs_probe)
    option(FFRS_LINK_NVN_DIAGNOSTICS "Include the NVN dispatch diagnostic surface when available" ON)
    set(FFRS_NVN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../graphics/nvn" CACHE PATH "NVN compatibility source directory")
    if(FFRS_LINK_NVN_DIAGNOSTICS AND EXISTS "${FFRS_NVN_DIR}/src/nvn_dispatch.c")
        add_library(ffrs_nvn_diagnostics STATIC "${FFRS_NVN_DIR}/src/nvn_dispatch.c")
        target_include_directories(ffrs_nvn_diagnostics PUBLIC "${FFRS_NVN_DIR}/include")
        target_link_libraries(ffrs_diagnostic_core PUBLIC ffrs_nvn_diagnostics)
        target_compile_definitions(ffrs_diagnostic_core PRIVATE FFRS_HAS_NVN_DIAGNOSTICS=1)
    endif()
    add_executable(nso_diagnostic diagnostic_main.cpp)
    target_link_libraries(nso_diagnostic PRIVATE ffrs_diagnostic_core)
    add_library(ffrs_diagnostic SHARED diagnostic_jni.cpp display_jni.cpp leaf_jni.cpp leaf_validation.cpp)
    target_link_libraries(ffrs_diagnostic PRIVATE ffrs_diagnostic_core)
    foreach(target probe_rg477m nso_diagnostic ffrs_diagnostic)
        target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--build-id=sha1")
    endforeach()

    # Experimental guest execution belongs to this standalone CLI only. Never
    # link HLE or install its fault handler in the load-only JNI diagnostic.
    option(FFRS_BUILD_RUNTIME_HLE "Cross-build the standalone strict HLE CLI" OFF)
    if(FFRS_BUILD_RUNTIME_HLE)
        set(FFRS_HLE_DIR "${FFRS_RUNTIME_DIR}/../hle" CACHE PATH "Typed runtime HLE source directory")
        add_executable(nso_runtime_hle "${FFRS_RUNTIME_DIR}/nso_call.c"
            "${FFRS_RUNTIME_DIR}/nso_loader.c" "${FFRS_HLE_DIR}/nso_host.c"
            "${FFRS_HLE_DIR}/address_region.c" "${FFRS_HLE_DIR}/mutex.c"
            "${FFRS_HLE_DIR}/exit_registry.c")
        set_target_properties(nso_runtime_hle PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        target_include_directories(nso_runtime_hle PRIVATE "${FFRS_RUNTIME_DIR}" "${FFRS_HLE_DIR}")
        target_compile_definitions(nso_runtime_hle PRIVATE NSO_WITH_HOST_HLE=1)
        target_compile_options(nso_runtime_hle PRIVATE -Wall -Wextra -Werror -Wformat=2)
        target_link_libraries(nso_runtime_hle PRIVATE dl m)
        target_link_options(nso_runtime_hle PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384"
            "-Wl,--build-id=sha1" "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(FFRS_READELF "" CACHE FILEPATH "Android LLVM readelf used by the static build gate")
        if(NOT EXISTS "${FFRS_READELF}")
            message(FATAL_ERROR "Set FFRS_READELF to the Android toolchain llvm-readelf")
        endif()
        add_custom_command(TARGET nso_runtime_hle POST_BUILD
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/device/verify_runtime_hle_elf.py"
                --elf "$<TARGET_FILE:nso_runtime_hle>" --readelf "${FFRS_READELF}"
                --output "$<TARGET_FILE:nso_runtime_hle>.elf.json"
            VERBATIM)
    endif()
endif()
install(TARGETS probe_rg477m RUNTIME DESTINATION bin)
