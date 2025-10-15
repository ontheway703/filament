cmake_minimum_required(VERSION 3.19)
project(gltfio_ext)

set(TARGET gltfio_ext)
set(PUBLIC_HDR_DIR include)

# 公开头文件
set(PUBLIC_HDRS
    include/gltfio/SkeletonAsset.h
    include/gltfio/MeshAsset.h
    include/gltfio/AnimationAsset.h
    include/gltfio/AnimationPack.h
    include/gltfio/StandaloneAnimator.h
    include/gltfio/AssetLoaderExt.h
)

# 内部实现文件
set(SRCS
    src/FSkeletonAsset.h
    src/FMeshAsset.h
    src/FAnimationAsset.h
    src/FAnimationPack.h
    src/FStandaloneAnimator.h
    src/GltfioExtInternal.h
    src/AssetLoaderExt.cpp
    src/SkeletonAsset.cpp
    src/MeshAsset.cpp
    src/AnimationAsset.cpp
    src/AnimationPack.cpp
    src/StandaloneAnimator.cpp
)

# 构建静态库
add_library(${TARGET} STATIC ${PUBLIC_HDRS} ${SRCS})

# 包含目录
target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
target_include_directories(${TARGET} PRIVATE src)

# 链接依赖 (只链接gltfio_core，不链接完整gltfio)
target_link_libraries(${TARGET} PUBLIC
    filament
    gltfio_core
    math
    utils
)

# 编译选项
set(GLTFIO_EXT_WARNINGS -Wall -Werror)
if (NOT MSVC)
    target_compile_options(${TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
endif()

# 安装规则
install(TARGETS ${TARGET} ARCHIVE DESTINATION lib/${DIST_DIR})
install(DIRECTORY ${PUBLIC_HDR_DIR}/gltfio DESTINATION include)

# 设置文件夹
set_target_properties(${TARGET} PROPERTIES FOLDER Libs)

# 测试程序
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_executable(test_skeleton tests/test_skeleton.cpp)
    target_link_libraries(test_skeleton PRIVATE gltfio_ext)
    set_target_properties(test_skeleton PROPERTIES FOLDER Tests)

    add_executable(test_mesh tests/test_mesh.cpp)
    target_link_libraries(test_mesh PRIVATE gltfio_ext)
    set_target_properties(test_mesh PROPERTIES FOLDER Tests)

    add_executable(test_animation tests/test_animation.cpp)
    target_link_libraries(test_animation PRIVATE gltfio_ext)
    set_target_properties(test_animation PROPERTIES FOLDER Tests)

    add_executable(test_animator tests/test_animator.cpp)
    target_link_libraries(test_animator PRIVATE gltfio_ext)
    set_target_properties(test_animator PROPERTIES FOLDER Tests)

    add_executable(test_integration tests/test_integration.cpp)
    target_link_libraries(test_integration PRIVATE gltfio_ext)
    set_target_properties(test_integration PROPERTIES FOLDER Tests)

    # 新增的综合测试
    add_executable(test_edge_cases tests/test_edge_cases.cpp)
    target_link_libraries(test_edge_cases PRIVATE gltfio_ext)
    set_target_properties(test_edge_cases PROPERTIES FOLDER Tests)

    add_executable(test_multi_channel_bug tests/test_multi_channel_bug.cpp)
    target_link_libraries(test_multi_channel_bug PRIVATE gltfio_ext)
    set_target_properties(test_multi_channel_bug PROPERTIES FOLDER Tests)

    add_executable(test_animation_blending tests/test_animation_blending.cpp)
    target_link_libraries(test_animation_blending PRIVATE gltfio_ext)
    set_target_properties(test_animation_blending PROPERTIES FOLDER Tests)

    add_executable(test_performance tests/test_performance.cpp)
    target_link_libraries(test_performance PRIVATE gltfio_ext)
    set_target_properties(test_performance PROPERTIES FOLDER Tests)

    add_executable(test_equipment_system tests/test_equipment_system.cpp)
    target_link_libraries(test_equipment_system PRIVATE gltfio_ext)
    set_target_properties(test_equipment_system PROPERTIES FOLDER Tests)

    # 添加所有测试到总测试目标
    add_custom_target(test_gltfio_ext
        DEPENDS test_skeleton test_mesh test_animation test_animator test_integration
                test_edge_cases test_multi_channel_bug test_animation_blending
                test_performance test_equipment_system
    )
    set_target_properties(test_gltfio_ext PROPERTIES FOLDER Tests)

    # 创建核心测试套件（只包含基础功能测试）
    add_custom_target(test_gltfio_ext_core
        DEPENDS test_skeleton test_mesh test_animation test_animator
    )
    set_target_properties(test_gltfio_ext_core PROPERTIES FOLDER Tests)

    # 创建压力测试套件
    add_custom_target(test_gltfio_ext_stress
        DEPENDS test_edge_cases test_multi_channel_bug test_animation_blending
                test_performance test_equipment_system
    )
    set_target_properties(test_gltfio_ext_stress PROPERTIES FOLDER Tests)
endif()
