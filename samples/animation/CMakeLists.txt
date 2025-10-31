cmake_minimum_required(VERSION 3.19)
project(animation-samples C CXX)

# ==================================================================================================
# Find SDL2
# ==================================================================================================

find_package(SDL2 REQUIRED)

if (NOT SDL2_FOUND)
    message(WARNING "SDL2 not found. Animation samples will not be built.")
    return()
endif()

# ==================================================================================================
# Animation Common Library
# ==================================================================================================

# Create animation-common static library for shared utility functions
add_library(animation-common STATIC
    common/AnimationUtils.cpp
)

target_include_directories(animation-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(animation-common PUBLIC
    filament
    gltfio
    gltfio_ext
    utils
    ${SDL2_LIBRARIES}
)

# Platform-specific settings
if (APPLE)
    # Link macOS frameworks for Metal layer support
    target_link_libraries(animation-common PUBLIC
        "-framework QuartzCore"
        "-framework Cocoa"
    )
    # Enable Objective-C++ for macOS Metal layer support
    set_source_files_properties(common/AnimationUtils.cpp PROPERTIES
        COMPILE_FLAGS "-x objective-c++")
endif()

set_target_properties(animation-common PROPERTIES FOLDER Samples/Animation)

# ==================================================================================================
# Helper Function to Add Animation Samples
# ==================================================================================================

function(add_animation_sample NAME)
    # Create executable
    add_executable(${NAME} ${NAME}.cpp)

    # Set C++ standard
    set_target_properties(${NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        FOLDER Samples/Animation
    )

    # Include directories
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SDL2_INCLUDE_DIRS}
    )

    # Link libraries
    target_link_libraries(${NAME} PRIVATE
        animation-common
        filament
        gltfio
        gltfio_ext
        utils
        ${SDL2_LIBRARIES}
    )

    # Platform-specific settings
    if (APPLE)
        # Enable Objective-C++ for macOS
        set_source_files_properties(${NAME}.cpp PROPERTIES
            COMPILE_FLAGS "-x objective-c++")

        # Set install rpath (needed after XCode 15.3)
        set_target_properties(${NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH /usr/local/lib
        )
    endif()

    # Compiler warnings
    if (NOT MSVC)
        target_compile_options(${NAME} PRIVATE
            -Wno-extern-c-compat
            $<$<NOT:$<PLATFORM_ID:Linux>>:-Wno-address-of-packed-member>
        )
    endif()
endfunction()

# ==================================================================================================
# Add Animation Samples
# ==================================================================================================

# Sample 1: External Animation - Basic workflow
add_animation_sample(external_animation)

# Sample 2: animation_hotreload - Runtime management
add_animation_sample(animation_hotreload)

# Sample 3: multi_character_animation - Multi-instance sharing
add_animation_sample(multi_character_animation)
