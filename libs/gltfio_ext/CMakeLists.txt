cmake_minimum_required(VERSION 3.19)
project(gltfio_ext C ASM)

set(TARGET gltfio_ext)
set(PUBLIC_HDR_DIR include)

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ==================================================================================================
# Sources and headers
# ==================================================================================================
set(PUBLIC_HDRS
        include/gltfio_ext/AnimationAsset.h
        include/gltfio_ext/AnimationBinding.h
        include/gltfio_ext/Animator.h
        include/gltfio_ext/AssetLoader.h
        include/gltfio_ext/FilamentAsset.h
        include/gltfio_ext/FilamentInstance.h
        include/gltfio_ext/MaterialProvider.h
        include/gltfio_ext/NodeManager.h
        include/gltfio_ext/TrsTransformManager.h
        include/gltfio_ext/ResourceLoader.h
        include/gltfio_ext/TextureProvider.h
        include/gltfio_ext/math.h
)

set(SRCS
        src/AnimationAsset.cpp
        src/AnimationBinding.cpp
        src/ArchiveCache.cpp
        src/ArchiveCache.h
        src/Animator.cpp
        src/AssetLoader.cpp
        src/DependencyGraph.cpp
        src/DependencyGraph.h
        src/DracoCache.cpp
        src/DracoCache.h
        src/FFilamentAsset.h
        src/FFilamentInstance.h
        src/FilamentAsset.cpp
        src/FilamentInstance.cpp
        src/FNodeManager.h
        src/FTrsTransformManager.h
        src/GltfEnums.h
        src/Ktx2Provider.cpp
        src/MaterialProvider.cpp
        src/NodeManager.cpp
        src/TrsTransformManager.cpp
        src/ResourceLoader.cpp
        src/StbProvider.cpp
        src/TangentsJob.cpp
        src/TangentsJob.h
        src/UbershaderProvider.cpp
        src/Utility.cpp
        src/Utility.h
        src/Wireframe.cpp
        src/Wireframe.h
        src/downcast.h
        src/extended/AssetLoaderExtended.cpp
        src/extended/AssetLoaderExtended.h
        src/extended/ResourceLoaderExtended.cpp
        src/extended/ResourceLoaderExtended.h
        src/extended/TangentsJobExtended.cpp
        src/extended/TangentsJobExtended.h
        src/extended/TangentSpaceMeshWrapper.cpp
        src/extended/TangentSpaceMeshWrapper.h
)

# ==================================================================================================
# Build materials
# ==================================================================================================

set(RESOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR})

if (CMAKE_CROSSCOMPILING)
    include(${IMPORT_EXECUTABLES})
endif()

set(DOUBLESIDED false)
set(TRANSPARENCY default)

set(UBERZ_OUTPUT_PATH "${RESOURCE_DIR}/default.uberz")

set (MATC_FLAGS ${MATC_BASE_FLAGS})
if (FILAMENT_SAMPLES_STEREO_TYPE STREQUAL "instanced")
    set (MATC_FLAGS ${MATC_FLAGS} -PstereoscopicType=instanced)
elseif (FILAMENT_SAMPLES_STEREO_TYPE STREQUAL "multiview")
    set (MATC_FLAGS ${MATC_FLAGS} -PstereoscopicType=multiview)
endif ()

function(build_ubershader NAME SRC SHADINGMODEL BLENDING)
    set(DEST "${RESOURCE_DIR}/${NAME}")
    configure_file(materials/${SRC}.mat.in "${DEST}.mat" COPYONLY)
    configure_file(materials/${SRC}.spec.in "${DEST}.spec" COPYONLY)

    set(TEMPLATE_ARGS
        -TCUSTOM_PARAMS="// no custom params"
        -TCUSTOM_VERTEX="// no custom vertex"
        -TCUSTOM_FRAGMENT="// no custom fragment"
        -TDOUBLESIDED=false
        -TTRANSPARENCY=default
        -TSHADINGMODEL=${SHADINGMODEL}
        -TBLENDING=${BLENDING})

    add_custom_command(
        OUTPUT "${NAME}.filamat"
        COMMAND matc ${MATC_FLAGS} ${TEMPLATE_ARGS} -o "${NAME}.filamat" "${NAME}.mat"
        DEPENDS matc "${DEST}.mat"
        WORKING_DIRECTORY ${RESOURCE_DIR}
        COMMENT "Compiling material ${NAME}")

    set(UBERZ_ARGS -o ${UBERZ_OUTPUT_PATH} ${TEMPLATE_ARGS})

    # Invoke uberz to consume foo.spec + foo.filamat and produce default.uberz
    # The first time this is invoked, it omits the --append argument.
    if (NOT APPEND_UBERSHADERS)
        set(APPEND_UBERSHADERS ON PARENT_SCOPE)
        add_custom_command(
            OUTPUT ${UBERZ_OUTPUT_PATH}
            COMMAND uberz ${UBERZ_ARGS} ${NAME}
            DEPENDS uberz ${DEST}.filamat ${DEST}.spec
            WORKING_DIRECTORY ${RESOURCE_DIR}
            COMMENT "Creating ubershader archive")
    else()
        add_custom_command(
            OUTPUT ${UBERZ_OUTPUT_PATH}
            COMMAND uberz --append ${UBERZ_ARGS} ${NAME}
            DEPENDS uberz ${DEST}.filamat ${DEST}.spec
            WORKING_DIRECTORY ${RESOURCE_DIR}
            APPEND
            COMMENT "Appending to ubershader archive")
    endif()

endfunction()

# NOTE: The default ubershader material is defined as the first material in the list below.

build_ubershader(lit_opaque                base            lit opaque)
build_ubershader(lit_fade                  base            lit fade)
build_ubershader(lit_masked                base            lit masked)
build_ubershader(specularGlossiness_fade   base            specularGlossiness fade)
build_ubershader(specularGlossiness_opaque base            specularGlossiness opaque)
build_ubershader(specularGlossiness_masked base            specularGlossiness masked)
build_ubershader(unlit_fade                base            unlit fade)
build_ubershader(unlit_opaque              base            unlit opaque)
build_ubershader(unlit_masked              base            unlit masked)
build_ubershader(transmission_opaque       transmission    lit opaque)
build_ubershader(transmission_fade         transmission    lit fade)
build_ubershader(transmission_masked       transmission    lit masked)
build_ubershader(volume_opaque             volume          lit opaque)
build_ubershader(volume_fade               volume          lit fade)
build_ubershader(volume_masked             volume          lit masked)
build_ubershader(sheen                     sheen           _ _)
build_ubershader(specular_opaque           specular        lit opaque)
build_ubershader(specular_fade             specular        lit fade)
build_ubershader(specular_masked           specular        lit masked)

add_custom_target(uberz_file_ext DEPENDS ${UBERZ_OUTPUT_PATH})
set_target_properties(uberz_file_ext PROPERTIES FOLDER Libs)

# ==================================================================================================
# Build a resgen library for the ubershader archive.
# ==================================================================================================

get_resgen_vars("${RESOURCE_DIR}/materials" uberarchive)

add_custom_command(
        OUTPUT ${RESGEN_OUTPUTS}
        COMMAND resgen ${RESGEN_FLAGS} ${UBERZ_OUTPUT_PATH}
        DEPENDS resgen ${UBERZ_OUTPUT_PATH}
)

if (DEFINED RESGEN_SOURCE_FLAGS)
    set_source_files_properties(${RESGEN_SOURCE} PROPERTIES COMPILE_FLAGS ${RESGEN_SOURCE_FLAGS})
endif()

set(DUMMY_SRC "${RESOURCE_DIR}/dummy.c")
add_custom_command(OUTPUT ${DUMMY_SRC} COMMAND echo "//" > ${DUMMY_SRC})

add_library(uberarchive_ext ${DUMMY_SRC} ${RESGEN_SOURCE})
target_include_directories(uberarchive_ext PUBLIC ${RESOURCE_DIR})
set_target_properties(uberarchive_ext PROPERTIES FOLDER Libs)

# ==================================================================================================
# Build the "core" library (no filamat)
# ==================================================================================================

include_directories(${PUBLIC_HDR_DIR} ${RESOURCE_DIR})
link_libraries(math utils filament cgltf stb ktxreader geometry tsl uberzlib)

add_library(gltfio_ext_core STATIC ${PUBLIC_HDRS} ${SRCS})

target_include_directories(gltfio_ext_core PUBLIC ${PUBLIC_HDR_DIR})
set_target_properties(gltfio_ext_core PROPERTIES FOLDER Libs)

target_compile_definitions(gltfio_ext_core PUBLIC -DGLTFIO_DRACO_SUPPORTED=1)
target_link_libraries(gltfio_ext_core PUBLIC dracodec meshoptimizer)

if (WEBGL_PTHREADS)
    target_compile_definitions(gltfio_ext_core PUBLIC -DFILAMENT_WASM_THREADS)
endif()

set(GLTFIO_EXT_WARNINGS -Wall -Werror)
if (NOT MSVC)
    target_compile_options(gltfio_ext_core PRIVATE ${GLTFIO_WARNINGS})
endif()

if (NOT WEBGL AND NOT ANDROID AND NOT IOS)

    # ==================================================================================================
    # Link the core library with additional dependencies to create the "full" library
    # ==================================================================================================
    add_library(${TARGET} STATIC ${PUBLIC_HDRS} src/JitShaderProvider.cpp)
    target_link_libraries(${TARGET} PUBLIC filamat gltfio_ext_core)
    target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
    set_target_properties(${TARGET} PROPERTIES FOLDER Libs)
    if (NOT MSVC)
        target_compile_options(${TARGET} PRIVATE -Wno-deprecated-register)
    endif()

    install(TARGETS ${TARGET} ARCHIVE DESTINATION lib/${DIST_DIR})

endif()

# ==================================================================================================
# Tests
# ==================================================================================================

set(GLTF_TEST_FILES)
function(add_test_gltf SOURCE TARGET)
    set(source_path "${ROOT_DIR}/${SOURCE}")
    set(target_path "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}")
    add_custom_command(
        OUTPUT ${target_path}
        DEPENDS ${source_path}
        COMMAND ${CMAKE_COMMAND} -E copy ${source_path} ${target_path}
    )
    list(APPEND GLTF_TEST_FILES "${target_path}")
    set(GLTF_TEST_FILES ${GLTF_TEST_FILES} PARENT_SCOPE)
endfunction()

add_test_gltf("third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb" "AnimatedMorphCube.glb")
add_test_gltf("tmp/test_assets/ecorche_animation_only.glb" "ecorche_animation_only.glb")
add_test_gltf("tmp/test_assets/ecorche_full.glb" "ecorche_full.glb")
add_test_gltf("tmp/test_assets/ecorche_mesh_only.glb" "ecorche_mesh_only.glb")

add_custom_target(test_gltfio_ext_files DEPENDS ${GLTF_TEST_FILES})

# The following tests rely on private APIs that are stripped
# away in Release builds
if (TNT_DEV AND NOT WEBGL AND NOT ANDROID AND NOT IOS)
    set(TEST_TARGET test_gltfio_ext)

    add_executable(${TEST_TARGET} test/gltfio_test.cpp)
    add_dependencies(${TEST_TARGET} test_gltfio_ext_files)
    set_property(TARGET test_gltfio_ext PROPERTY LINK_LIBRARIES)

    target_link_libraries(${TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER Tests)

    # AnimationAsset unit test
    set(ANIMATION_ASSET_TEST_TARGET test_animation_asset)
    add_executable(${ANIMATION_ASSET_TEST_TARGET} test/test_animation_asset.cpp)
    target_link_libraries(${ANIMATION_ASSET_TEST_TARGET} PRIVATE gltfio_ext_core gtest)
    if (NOT MSVC)
        target_compile_options(${ANIMATION_ASSET_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATION_ASSET_TEST_TARGET} PROPERTIES FOLDER Tests)

    # AssetLoader::loadAnimationAsset test
    set(ASSET_LOADER_TEST_TARGET test_asset_loader)
    add_executable(${ASSET_LOADER_TEST_TARGET} test/test_asset_loader.cpp)
    add_dependencies(${ASSET_LOADER_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ASSET_LOADER_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ASSET_LOADER_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ASSET_LOADER_TEST_TARGET} PROPERTIES FOLDER Tests)

    # AnimationBinding test
    set(ANIMATION_BINDING_TEST_TARGET test_animation_binding)
    add_executable(${ANIMATION_BINDING_TEST_TARGET} test/test_animation_binding.cpp)
    add_dependencies(${ANIMATION_BINDING_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ANIMATION_BINDING_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ANIMATION_BINDING_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATION_BINDING_TEST_TARGET} PROPERTIES FOLDER Tests)

    # Bone matrices test (updateBoneMatrices / resetBoneMatrices with large buffer)
    set(BONE_MATRICES_TEST_TARGET test_bone_matrices)
    add_executable(${BONE_MATRICES_TEST_TARGET} test/test_bone_matrices.cpp)
    add_dependencies(${BONE_MATRICES_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${BONE_MATRICES_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${BONE_MATRICES_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${BONE_MATRICES_TEST_TARGET} PROPERTIES FOLDER Tests)

    # Animator tests (external animation support) - Split into 4 files by functionality
    # NOTE: Replaced single test_animator.cpp with 4 specialized test files

    # Animator lifecycle test (3 tests: lifecycle and resource management)
    set(ANIMATOR_LIFECYCLE_TEST_TARGET test_animator_lifecycle)
    add_executable(${ANIMATOR_LIFECYCLE_TEST_TARGET} test/test_animator_lifecycle.cpp)
    add_dependencies(${ANIMATOR_LIFECYCLE_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ANIMATOR_LIFECYCLE_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ANIMATOR_LIFECYCLE_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATOR_LIFECYCLE_TEST_TARGET} PROPERTIES FOLDER Tests)

    # Animator playback test (11 tests: source management, playback, query, integration)
    set(ANIMATOR_PLAYBACK_TEST_TARGET test_animator_playback)
    add_executable(${ANIMATOR_PLAYBACK_TEST_TARGET} test/test_animator_playback.cpp)
    add_dependencies(${ANIMATOR_PLAYBACK_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ANIMATOR_PLAYBACK_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ANIMATOR_PLAYBACK_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATOR_PLAYBACK_TEST_TARGET} PROPERTIES FOLDER Tests)

    # Animator cache test (3 tests: LRU eviction, cache size, statistics)
    set(ANIMATOR_CACHE_TEST_TARGET test_animator_cache)
    add_executable(${ANIMATOR_CACHE_TEST_TARGET} test/test_animator_cache.cpp)
    add_dependencies(${ANIMATOR_CACHE_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ANIMATOR_CACHE_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ANIMATOR_CACHE_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATOR_CACHE_TEST_TARGET} PROPERTIES FOLDER Tests)

    # Animator cross-fade test (7 tests: cross-fade, complex integration)
    set(ANIMATOR_CROSSFADE_TEST_TARGET test_animator_crossfade)
    add_executable(${ANIMATOR_CROSSFADE_TEST_TARGET} test/test_animator_crossfade.cpp)
    add_dependencies(${ANIMATOR_CROSSFADE_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ANIMATOR_CROSSFADE_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ANIMATOR_CROSSFADE_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATOR_CROSSFADE_TEST_TARGET} PROPERTIES FOLDER Tests)

    # Animation cache test (Phase 2-3 lightweight cache)
    set(ANIMATION_CACHE_TEST_TARGET test_animation_cache)
    add_executable(${ANIMATION_CACHE_TEST_TARGET} test/test_animation_cache.cpp)
    add_dependencies(${ANIMATION_CACHE_TEST_TARGET} test_gltfio_ext_files)
    target_link_libraries(${ANIMATION_CACHE_TEST_TARGET} PRIVATE ${TARGET} gtest uberarchive_ext)
    if (NOT MSVC)
        target_compile_options(${ANIMATION_CACHE_TEST_TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
    endif()
    set_target_properties(${ANIMATION_CACHE_TEST_TARGET} PROPERTIES FOLDER Tests)
endif()

# ==================================================================================================
# Installation
# ==================================================================================================

install(TARGETS gltfio_ext_core uberarchive_ext ARCHIVE DESTINATION lib/${DIST_DIR})
install(DIRECTORY ${PUBLIC_HDR_DIR}/gltfio_ext DESTINATION include)
install(FILES ${RESOURCE_DIR}/materials/uberarchive.h DESTINATION include/gltfio_ext/materials)
