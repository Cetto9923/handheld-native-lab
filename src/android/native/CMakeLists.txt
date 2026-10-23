cmake_minimum_required(VERSION 3.22)
project(probe_rg477m LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Common probe sources
set(PROBE_SOURCES
    main.cpp
    probe_core.cpp
    probe_types.cpp
    probe_platform.cpp
    json_writer.cpp
)

if(ANDROID)
    message(STATUS "Building for Android:")
    message(STATUS "  ABI: ${ANDROID_ABI}")
    message(STATUS "  Platform: ${ANDROID_PLATFORM}")
    message(STATUS "  NDK: ${ANDROID_NDK}")

    list(APPEND PROBE_SOURCES
        probe_android.cpp
        probe_vulkan.cpp
    )

    add_executable(probe_rg477m ${PROBE_SOURCES})

    target_compile_definitions(probe_rg477m PRIVATE
        __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
    )

    target_link_libraries(probe_rg477m PRIVATE
        vulkan
        android
        log
    )

    # Ensure position independent executable for Android modern Bionic
    set_target_properties(probe_rg477m PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
else()
    message(STATUS "Building for Host (${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR})")

    list(APPEND PROBE_SOURCES
        probe_host.cpp
        probe_vulkan.cpp
    )

    add_executable(probe_rg477m ${PROBE_SOURCES})
endif()

# Install / output directory
install(TARGETS probe_rg477m RUNTIME DESTINATION bin)
