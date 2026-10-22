#!/bin/bash
# ============================================================
# OpenTenBase 一键基准测试脚本
# Issue #202: 基准性能测试方案设计与 AI 辅助分析
# ============================================================
#
# 用法: ./run_all_benchmarks.sh <CN_HOST> [CN_PORT] [DB_USER]
#
# 默认值: CN_PORT=11000, DB_USER=opentenbase
#
# ============================================================

set -euo pipefail

CN_HOST="${1:?请提供 CN_HOST 参数，例如: ./run_all_benchmarks.sh 127.0.0.1}"
CN_PORT="${2:-11000}"
DB_USER="${3:-opentenbase}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== OpenTenBase 一键基准测试 ==="
echo "CN: ${CN_HOST}:${CN_PORT}  用户: ${DB_USER}"
echo ""

# ── 前置检查 ──
echo "[1/6] 检查连接..."
if ! psql -h "${CN_HOST}" -p "${CN_PORT}" -U "${DB_USER}" -d postgres -c '\q' 2>/dev/null; then
    echo "❌ 无法连接 CN (${CN_HOST}:${CN_PORT})，请检查集群状态"
    echo "   确认命令: opentenbase_ctl status"
    exit 1
fi
echo "✅ 连接成功"

# ── 初始化 ──
echo ""
echo "[2/6] 初始化 Schema..."
psql -h "${CN_HOST}" -p "${CN_PORT}" -U "${DB_USER}" -d postgres -f "${SCRIPT_DIR}/01_schema_init.sql"

# ── 数据加载 ──
echo ""
echo "[3/6] 加载测试数据..."
psql -h "${CN_HOST}" -p "${CN_PORT}" -U "${DB_USER}" -d postgres -f "${SCRIPT_DIR}/02_data_load.sql"

# ── 基准查询 ──
echo ""
echo "[4/6] 运行基准查询 (EXPLAIN ANALYZE)..."
psql -h "${CN_HOST}" -p "${CN_PORT}" -U "${DB_USER}" -d postgres -f "${SCRIPT_DIR}/03_benchmark_queries.sql"

# ── 并发测试 ──
echo ""
echo "[5/6] 并发压力测试 (pgbench)..."
"${SCRIPT_DIR}/04_pgbench_scripts.sh" "${CN_HOST}" "${CN_PORT}" "${DB_USER}"

# ── 分布分析 ──
echo ""
echo "[6/6] 分布特征分析..."
psql -h "${CN_HOST}" -p "${CN_PORT}" -U "${DB_USER}" -d postgres -f "${SCRIPT_DIR}/05_distribution_analysis.sql"

echo ""
echo "✅ 全量基准测试完成！"
echo "📝 请将 pgbench 结果填入 results_summary_template.md"
echo "🧹 若需清理测试数据: psql -h ${CN_HOST} -p ${CN_PORT} -U ${DB_USER} -d postgres -f ${SCRIPT_DIR}/06_cleanup.sql"
