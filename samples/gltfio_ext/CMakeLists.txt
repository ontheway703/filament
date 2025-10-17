cmake_minimum_required(VERSION 3.19)

# ==================================================================================================
# Common helper library for gltfio_ext samples
# ==================================================================================================

add_library(gltfio_ext_common STATIC
    common/SDLHelper.cpp
    common/FilamentHelper.cpp
)

target_include_directories(gltfio_ext_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(gltfio_ext_common PUBLIC
    filament
    sdl2
)

# Platform-specific settings
if(APPLE)
    target_link_libraries(gltfio_ext_common PUBLIC
        "-framework QuartzCore"
        "-framework Cocoa"
    )
    # Enable Objective-C++ for macOS Metal layer support
    set_source_files_properties(common/SDLHelper.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

set_target_properties(gltfio_ext_common PROPERTIES FOLDER Samples/gltfio_ext)

# ==================================================================================================
# Function to add gltfio_ext samples
# ==================================================================================================

function(add_gltfio_ext_sample target_name source_file)
    # Create executable
    add_executable(${target_name} ${source_file})

    # Include directories
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GENERATION_ROOT}
    )

    # Link libraries
    target_link_libraries(${target_name} PRIVATE
        gltfio_ext_common
        gltfio_ext
        uberarchive
        filament
        sdl2
        sample-resources
    )

    # Platform-specific linking
    if(APPLE)
        target_link_libraries(${target_name} PRIVATE
            "-framework QuartzCore"
            "-framework Cocoa"
        )
        # Enable Objective-C++ for macOS
        set_source_files_properties(${source_file} PROPERTIES COMPILE_FLAGS "-x objective-c++")

        # This is needed after XCode 15.3
        set_target_properties(${target_name} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
        set_target_properties(${target_name} PROPERTIES INSTALL_RPATH /usr/local/lib)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(${target_name} PRIVATE
            GL
            pthread
            dl
        )
    endif()

    # Compiler flags
    if (MSVC)
        # MSVC specific flags
    else()
        target_compile_options(${target_name} PRIVATE
            -Wno-extern-c-compat
            $<$<NOT:$<PLATFORM_ID:Linux>>:-Wno-address-of-packed-member>
        )
    endif()

    set_target_properties(${target_name} PROPERTIES FOLDER Samples/gltfio_ext)
endfunction()

# ==================================================================================================
# Add gltfio_ext samples
# ==================================================================================================

if (NOT ANDROID)
    add_gltfio_ext_sample(gltfio_ext_basic gltfio_ext_basic.cpp)
    add_gltfio_ext_sample(gltfio_ext_multichannel gltfio_ext_multichannel.cpp)
    add_gltfio_ext_sample(gltfio_ext_equipment gltfio_ext_equipment.cpp)
endif()
