-- ============================================================
-- OpenTenBase 清理脚本
-- 删除所有基准测试表和数据
-- ============================================================

\echo '🧹 开始清理基准测试数据...'

DROP TABLE IF EXISTS bench_hash_orders;
DROP TABLE IF EXISTS bench_rep_customers;
DROP TABLE IF EXISTS bench_modulo_products;
DROP TABLE IF EXISTS bench_shard_transactions;
DROP TABLE IF EXISTS bench_hash_logs;

\echo '✅ 所有基准测试表已删除。'
