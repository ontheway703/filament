#!/bin/bash

# gltfio_ext 单元测试运行脚本
# 运行所有 gltfio_ext 相关的单元测试

set -e  # 遇到错误立即退出

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

# 运行每个测试
for test_name in "${TESTS[@]}"; do
    echo -e "${YELLOW}运行测试: $test_name${NC}"
    echo "----------------------------------------"

    if [ ! -f "./$test_name" ]; then
        echo -e "${RED}警告: 测试文件不存在: $test_name${NC}"
        echo ""
        continue
    fi

    # 清空临时文件
    > /tmp/gltfio_test_output.txt

    # 运行测试并捕获输出
    if ./"$test_name" 2>&1 | tee /tmp/gltfio_test_output.txt; then
        # 提取测试结果
        TEST_RESULT=$(grep -E "\[==========\].*ran\." /tmp/gltfio_test_output.txt || echo "")
        PASSED=$(grep -E "\[  PASSED  \] [0-9]+ tests?" /tmp/gltfio_test_output.txt || echo "")
        SKIPPED=$(grep -E "\[  SKIPPED \] [0-9]+ tests?" /tmp/gltfio_test_output.txt || echo "")

        # 提取通过的测试数量
        if [ -n "$PASSED" ]; then
            PASSED_COUNT=$(echo "$PASSED" | grep -oE "[0-9]+ tests?" | grep -oE "[0-9]+" || echo "0")
            if [ -n "$PASSED_COUNT" ] && [ "$PASSED_COUNT" -gt 0 ]; then
                PASSED_TESTS=$((PASSED_TESTS + PASSED_COUNT))
                TOTAL_TESTS=$((TOTAL_TESTS + PASSED_COUNT))
            fi
        fi

        # 提取跳过的测试数量
        if [ -n "$SKIPPED" ]; then
            SKIPPED_COUNT=$(echo "$SKIPPED" | grep -oE "[0-9]+ tests?" | grep -oE "[0-9]+" || echo "0")
            if [ -n "$SKIPPED_COUNT" ] && [ "$SKIPPED_COUNT" -gt 0 ]; then
                SKIPPED_TESTS=$((SKIPPED_TESTS + SKIPPED_COUNT))
                TOTAL_TESTS=$((TOTAL_TESTS + SKIPPED_COUNT))
            fi
        fi

        # 输出结果摘要
        if [ -n "$PASSED_COUNT" ] && [ "$PASSED_COUNT" -gt 0 ] && [ -n "$SKIPPED_COUNT" ] && [ "$SKIPPED_COUNT" -gt 0 ]; then
            echo -e "${GREEN}✓ $test_name: $PASSED_COUNT 个通过${NC}, ${YELLOW}$SKIPPED_COUNT 个跳过${NC}"
        elif [ -n "$PASSED_COUNT" ] && [ "$PASSED_COUNT" -gt 0 ]; then
            echo -e "${GREEN}✓ $test_name: $PASSED_COUNT 个测试通过${NC}"
        elif [ -n "$SKIPPED_COUNT" ] && [ "$SKIPPED_COUNT" -gt 0 ]; then
            echo -e "${YELLOW}⊘ $test_name: $SKIPPED_COUNT 个测试跳过${NC}"
        else
            echo -e "${GREEN}✓ $test_name: 通过${NC}"
        fi
    else
        # 测试失败
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo -e "${RED}✗ $test_name: 失败${NC}"
        echo -e "${RED}详细输出见上方${NC}"
    fi

    echo ""
done

# 清理临时文件
rm -f /tmp/gltfio_test_output.txt

# 输出总结
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}测试总结${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "总测试数: ${TOTAL_TESTS}"
echo -e "${GREEN}通过: ${PASSED_TESTS}${NC}"
echo -e "${YELLOW}跳过: ${SKIPPED_TESTS}${NC}"
echo -e "${RED}失败: ${FAILED_TESTS}${NC}"
echo ""

if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}部分测试失败！${NC}"
    exit 1
elif [ $SKIPPED_TESTS -gt 0 ]; then
    echo -e "${GREEN}所有可执行测试通过！${NC} ${YELLOW}($SKIPPED_TESTS 个测试因环境限制跳过)${NC}"
    exit 0
else
    echo -e "${GREEN}所有测试通过！ ✓${NC}"
    exit 0
fi
