cmake_minimum_required(VERSION 3.10)

project(VulkanEngine C)


# Set the standard and export compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# Check if the VULKAN_SDK environment variable is set
if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK environment variable is not set")
endif()

# Set Vulkan SDK paths
set(VULKAN_SDK "$ENV{VULKAN_SDK}")
set(VULKAN_INCLUDE_DIR "${VULKAN_SDK}/include")
set(VULKAN_LIBRARY_DIR "${VULKAN_SDK}/lib")
include_directories(${VULKAN_INCLUDE_DIR})

# Find required packages
find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

file(GLOB SOURCES "src/*.c")  # Collect all .c files in the src folder

# Include the external directory for external libraries
include_directories(include)
include_directories(external)  # Suppresses warnings from external libraries
include_directories(${SDL2_INCLUDE_DIRS})  # Include SDL2 headers

# Include cJSON library
add_library(cjson STATIC external/cjson/cJSON.c)  # Add the cJSON library
include_directories(external/cjson)  # Include cJSON headers

# Include CGLM
include_directories(/opt/homebrew/opt/cglm/include)
link_directories(/opt/homebrew/opt/cglm/lib)

# Create the VulkanEngine executable
add_executable(VulkanEngine ${SOURCES}
        src/main.c)

# Link necessary libraries
target_link_libraries(VulkanEngine
        Vulkan::Vulkan
        ${VULKAN_LIBRARY_DIR}/libMoltenVK.dylib
        ${SDL2_LIBRARIES}  # Link SDL2
        cjson  # Link cJSON library
)

# Set the default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Set flags for different build types
set(CMAKE_C_FLAGS_DEBUG "-g")  # Debug flags for C
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")  # Optimize for release and disable assertions

# Disable interprocedural optimization for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

# Add Clang-specific warning flags and sanitizers only for Debug builds
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(VulkanEngine PRIVATE
            # Add C warning flags here, such as:
            -Wall
            -Wextra
            -Wpedantic
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(VulkanEngine PRIVATE
                -fsanitize=address
                -fsanitize=undefined
        )
    endif()
endif()