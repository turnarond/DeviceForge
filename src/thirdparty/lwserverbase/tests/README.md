# lwserverbase 测试框架

## 概述

本测试框架为 lwserverbase 的 MetricsCollector 组件提供全面的测试覆盖，包括：

- **Counter 指标测试**：计数器功能、边界情况、并发测试
- **Gauge 指标测试**：仪表盘功能、小数精度、并发测试
- **Histogram 指标测试**：直方图功能、统计计算、并发测试
- **Prometheus 导出测试**：格式验证、标签处理、兼容性测试
- **高级测试**：性能测试、压力测试、边界情况、稳定性测试

## 编译测试

### 前提条件

确保已构建 lwserverbase 库及其依赖项（lwcomm, lwevent, lwlog）。

### 编译步骤

```bash
cd /home/yanchaodong/work/acoinfo/vsoa_framework/src/ext/lwserverbase

# 创建构建目录
mkdir -p build && cd build

# 配置 CMake，启用测试
cmake .. -DBUILD_TESTS=ON

# 编译
make
```

## 运行测试

### 运行所有测试

```bash
# 使用 CTest
ctest

# 或手动运行
./tests/test_counter
./tests/test_gauge
./tests/test_histogram
./tests/test_prometheus_export
./tests/test_advanced
```

### 运行特定测试套件

```bash
# 运行 Counter 测试
./tests/test_counter

# 运行 Gauge 测试
./tests/test_gauge

# 运行 Histogram 测试
./tests/test_histogram

# 运行 Prometheus 导出测试
./tests/test_prometheus_export

# 运行高级测试（性能、并发等）
./tests/test_advanced
```

### 使用 CTest 的详细选项

```bash
# 显示详细信息
ctest -V

# 运行特定测试
ctest -R CounterTests

# 并行运行测试
ctest -j4

# 输出测试结果到文件
ctest -T Test --output-on-failure
```

## 测试用例详解

### 1. Counter 测试 (TestCounter.cpp)

**测试覆盖：**
- `TestCounterCreation` - 创建计数器
- `TestCounterIncrement` - 递增操作
- `TestCounterNegativeIncrement` - 负数递增
- `TestCounterZeroIncrement` - 零值递增
- `TestCounterLargeValues` - 大数值处理
- `TestCounterWithLabels` - 带标签的计数器
- `TestCounterGetOrCreate` - 获取或创建语义
- `TestCounterConcurrentIncrement` - 并发递增
- `TestCounterMixedConcurrentOperations` - 混合并发操作
- `TestCounterExportWithPrometheus` - Prometheus 导出

### 2. Gauge 测试 (TestGauge.cpp)

**测试覆盖：**
- `TestGaugeCreation` - 创建仪表盘
- `TestGaugeSet` - 设置值
- `TestGaugeIncrement` - 递增
- `TestGaugeDecrement` - 递减
- `TestGaugeZeroOperations` - 零值操作
- `TestGaugeLargeValues` - 大数值
- `TestGaugeWithLabels` - 带标签
- `TestGaugeGetOrCreate` - 获取或创建
- `TestGaugeConcurrentSet` - 并发设置
- `TestGaugeConcurrentIncrementDecrement` - 并发增减
- `TestGaugeRandomOperations` - 随机操作
- `TestGaugeExportWithPrometheus` - Prometheus 导出
- `TestGaugeSystemMetrics` - 系统指标

### 3. Histogram 测试 (TestHistogram.cpp)

**测试覆盖：**
- `TestHistogramCreation` - 创建直方图
- `TestHistogramObserve` - 观察值
- `TestHistogramZeroObservation` - 零值观察
- `TestHistogramNegativeValues` - 负数值
- `TestHistogramLargeValues` - 大数值
- `TestHistogramManyObservations` - 大量观察
- `TestHistogramWithLabels` - 带标签
- `TestHistogramGetOrCreate` - 获取或创建
- `TestHistogramConcurrentObservations` - 并发观察
- `TestHistogramRandomObservations` - 随机观察
- `TestHistogramExportWithPrometheus` - Prometheus 导出
- `TestHistogramExportWithLabels` - 带标签导出
- `TestHistogramDistributionStats` - 分布统计

### 4. Prometheus 导出测试 (TestPrometheusExport.cpp)

**测试覆盖：**
- `TestEmptyExport` - 空导出
- `TestBasicExportFormat` - 基本格式
- `TestExportLineEndings` - 行尾格式
- `TestCounterExportFormat` - Counter 导出格式
- `TestGaugeExportFormat` - Gauge 导出格式
- `TestHistogramExportFormat` - Histogram 导出格式
- `TestMixedTypesExport` - 混合类型导出
- `TestLabelExportFormat` - 标签导出格式
- `TestMultipleLabelsExport` - 多标签导出
- `TestSpecialCharactersInHelp` - 帮助文本特殊字符
- `TestMetricNameValidation` - 指标名验证
- `TestLargeExport` - 大量指标导出
- `TestExportIdempotency` - 导出幂等性
- `TestExportAfterModifications` - 修改后导出
- `TestSystemMetricsExport` - 系统指标导出
- `TestExportConstCorrectness` - const 正确性

### 5. 高级测试 (TestAdvanced.cpp)

**边界情况测试：**
- `TestExtremeCounterValues` - 极端 Counter 值
- `TestExtremeGaugeValues` - 极端 Gauge 值（包括 NaN、Inf）
- `TestEmptyMetricName` - 空指标名
- `TestVeryLongMetricName` - 超长指标名
- `TestSpecialCharactersInLabels` - 标签特殊字符
- `TestUnicodeInLabels` - Unicode 标签

**性能测试：**
- `TestCounterCreationPerformance` - Counter 创建性能
- `TestExportPerformance` - 导出性能
- `TestHighFrequencyUpdates` - 高频更新（100 万次/秒）

**并发压力测试：**
- `TestMassiveConcurrency` - 大规模并发（50 线程）
- `TestMixedOperationsConcurrency` - 混合操作并发
- `TestConcurrentExportAndModify` - 并发导出和修改

**稳定性测试：**
- `TestRepeatedCreateAndGet` - 重复创建和获取
- `TestCollectorLifetime` - 生命周期管理
- `TestRapidCreationDestruction` - 快速创建销毁

## 测试框架特性

### 断言宏

```cpp
// 基本断言
TEST_ASSERT(condition, message)

// 相等断言
TEST_ASSERT_EQ(expected, actual, message)

// 近似相等断言（用于浮点数）
TEST_ASSERT_NEAR(expected, actual, epsilon, message)
```

### 测试套件定义

```cpp
DEFINE_TEST_SUITE(SuiteName);

ADD_TEST(TestName) 
    // 测试代码
END_TEST;
```

## 性能基准

测试框架包含性能基准测试，典型结果：

- **Counter 创建**：10,000 个 Counter < 5 秒
- **导出性能**：3,000 个指标 < 100ms/次
- **高频更新**：> 1,000,000 次更新/秒
- **并发能力**：50 线程，10,000 次操作/线程

## 故障排除

### 常见问题

1. **编译错误：找不到头文件**
   ```bash
   # 确保包含目录正确
   target_include_directories(your_test PRIVATE ${LWSERVERBASE_DIR}/tests)
   ```

2. **链接错误：找不到库**
   ```bash
   # 确保链接了 lwserverbase 库
   target_link_libraries(your_test PRIVATE lwserverbase)
   ```

3. **测试失败：断言错误**
   - 检查测试逻辑
   - 验证预期值是否正确
   - 考虑浮点数精度问题（使用 TEST_ASSERT_NEAR）

### 调试测试

```bash
# 使用 GDB 调试
gdb ./tests/test_counter
(gdb) run

# 输出详细信息
ctest -V -R TestName
```

## 添加新测试

1. 在 `tests/` 目录创建新的测试文件
2. 包含必要的头文件：
   ```cpp
   #include <metrics/MetricsCollector.h>
   #include "TestFramework.h"
   ```
3. 编写测试用例
4. 在 CMakeLists.txt 中添加测试可执行文件
5. 注册到 CTest

## 最佳实践

1. **测试命名**：使用描述性名称，如 `TestCounterIncrement`
2. **断言消息**：提供清晰的错误消息
3. **测试隔离**：每个测试应该独立，不依赖其他测试
4. **清理资源**：使用 RAII，避免内存泄漏
5. **并发测试**：使用原子操作和适当的同步
6. **性能测试**：输出性能指标，便于基准比较

## 持续集成

测试框架设计为易于集成到 CI/CD 流程：

```yaml
# 示例：GitHub Actions
- name: Build and Test
  run: |
    mkdir build && cd build
    cmake .. -DBUILD_TESTS=ON
    make
    ctest --output-on-failure
```

## 联系与支持

如有问题或建议，请联系：
- Author: Yan.chaodong <yanchaodong@acoinfo.com>
- Date: 2026-03-10
