#!/bin/bash

# gltfio_ext 单元测试运行脚本
# 运行所有 gltfio_ext 相关的单元测试

set -uo pipefail

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试结果统计
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
FAILED_EXECUTABLES=0
MISSING_FIXTURE_SKIPS=0

# 获取脚本所在目录的根目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/../.."

# 测试目录
TEST_DIR="$PROJECT_ROOT/out/cmake-debug/libs/gltfio_ext"

# 测试列表（按逻辑分组）
TESTS=(
    # 核心单元测试
    "test_animation_asset"
    "test_gltfio_ext"
    "test_asset_loader"
    "test_animation_binding"
    "test_bone_matrices"
    # Animator 功能测试
    "test_animator_lifecycle"
    "test_animator_playback"
    "test_animator_cache"
    "test_animator_crossfade"
    # 缓存系统测试
    "test_animation_cache"
)

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}gltfio_ext 单元测试套件${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 检查测试目录是否存在
if [ ! -d "$TEST_DIR" ]; then
    echo -e "${RED}错误: 测试目录不存在: $TEST_DIR${NC}"
    echo -e "${YELLOW}请先运行编译: ./build.sh debug${NC}"
    exit 1
fi

# 切换到测试目录
cd "$TEST_DIR"

REQUIRED_FIXTURES=(
    "AnimatedMorphCube.glb"
    "ecorche_animation_only.glb"
    "ecorche_full.glb"
    "ecorche_mesh_only.glb"
)

for fixture in "${REQUIRED_FIXTURES[@]}"; do
    if [ ! -f "$fixture" ]; then
        echo -e "${RED}错误: 缺少测试 fixture: $TEST_DIR/$fixture${NC}"
        FAILED_EXECUTABLES=$((FAILED_EXECUTABLES + 1))
    fi
done

if [ "$FAILED_EXECUTABLES" -gt 0 ]; then
    echo -e "${RED}fixture staging 未完成；缺失依赖不能视为测试通过。${NC}"
    exit 1
fi

OUTPUT_FILE=$(mktemp "${TMPDIR:-/tmp}/gltfio_ext_tests.XXXXXX")
trap 'rm -f "$OUTPUT_FILE"' EXIT

# 运行每个测试
for test_name in "${TESTS[@]}"; do
    echo -e "${YELLOW}运行测试: $test_name${NC}"
    echo "----------------------------------------"

    if [ ! -f "./$test_name" ]; then
        echo -e "${RED}错误: 测试文件不存在: $test_name${NC}"
        FAILED_EXECUTABLES=$((FAILED_EXECUTABLES + 1))
        echo ""
        continue
    fi

    PASSED_COUNT=0
    FAILED_COUNT=0
    SKIPPED_COUNT=0
    : > "$OUTPUT_FILE"

    ./"$test_name" > "$OUTPUT_FILE" 2>&1
    TEST_STATUS=$?
    cat "$OUTPUT_FILE"

    PASSED_COUNT=$(sed -nE 's/^\[  PASSED  \] ([0-9]+) tests?\.$/\1/p' "$OUTPUT_FILE" | head -n 1)
    FAILED_COUNT=$(sed -nE 's/^\[  FAILED  \] ([0-9]+) tests?, listed below:$/\1/p' "$OUTPUT_FILE" | head -n 1)
    SKIPPED_COUNT=$(sed -nE 's/^\[  SKIPPED \] ([0-9]+) tests?(, listed below:|\.)$/\1/p' "$OUTPUT_FILE" | head -n 1)
    PASSED_COUNT=${PASSED_COUNT:-0}
    FAILED_COUNT=${FAILED_COUNT:-0}
    SKIPPED_COUNT=${SKIPPED_COUNT:-0}

    PASSED_TESTS=$((PASSED_TESTS + PASSED_COUNT))
    FAILED_TESTS=$((FAILED_TESTS + FAILED_COUNT))
    SKIPPED_TESTS=$((SKIPPED_TESTS + SKIPPED_COUNT))
    TOTAL_TESTS=$((TOTAL_TESTS + PASSED_COUNT + FAILED_COUNT + SKIPPED_COUNT))

    if [ "$SKIPPED_COUNT" -gt 0 ] && grep -Eq 'Test assets? not found|Test files?.*not found|fixture.*not found' "$OUTPUT_FILE"; then
        MISSING_FIXTURE_SKIPS=$((MISSING_FIXTURE_SKIPS + SKIPPED_COUNT))
    fi

    if [ "$TEST_STATUS" -ne 0 ] || [ "$FAILED_COUNT" -gt 0 ]; then
        if [ "$FAILED_COUNT" -eq 0 ]; then
            FAILED_EXECUTABLES=$((FAILED_EXECUTABLES + 1))
        fi
        echo -e "${RED}✗ $test_name: $FAILED_COUNT 个失败 (exit $TEST_STATUS)${NC}"
    elif [ "$PASSED_COUNT" -eq 0 ] && [ "$SKIPPED_COUNT" -eq 0 ]; then
        FAILED_EXECUTABLES=$((FAILED_EXECUTABLES + 1))
        echo -e "${RED}✗ $test_name: 未找到 GoogleTest 结果摘要${NC}"
    elif [ "$SKIPPED_COUNT" -gt 0 ]; then
        echo -e "${GREEN}✓ $test_name: $PASSED_COUNT 个通过${NC}, ${YELLOW}⊘ $SKIPPED_COUNT 个跳过${NC}"
    else
        echo -e "${GREEN}✓ $test_name: $PASSED_COUNT 个测试通过${NC}"
    fi

    echo ""
done

# 输出总结
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}测试总结${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "总测试数: ${TOTAL_TESTS}"
echo -e "${GREEN}通过: ${PASSED_TESTS}${NC}"
echo -e "${YELLOW}跳过: ${SKIPPED_TESTS}${NC}"
echo -e "${RED}失败: ${FAILED_TESTS}${NC}"
echo -e "${RED}执行错误: ${FAILED_EXECUTABLES}${NC}"
echo ""

if [ "$MISSING_FIXTURE_SKIPS" -gt 0 ]; then
    echo -e "${RED}$MISSING_FIXTURE_SKIPS 个测试因 fixture 缺失而跳过；该结果按失败处理。${NC}"
    exit 1
elif [ "$FAILED_TESTS" -gt 0 ] || [ "$FAILED_EXECUTABLES" -gt 0 ]; then
    echo -e "${RED}部分测试失败！${NC}"
    exit 1
elif [ "$SKIPPED_TESTS" -gt 0 ]; then
    echo -e "${GREEN}所有可执行测试通过！${NC} ${YELLOW}($SKIPPED_TESTS 个测试明确跳过，不计为通过)${NC}"
    exit 0
else
    echo -e "${GREEN}所有测试通过！ ✓${NC}"
    exit 0
fi
