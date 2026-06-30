# OpenTenBase 基准性能测试方案

> **Issue**: [#202 — OpenTenBase 基准性能测试方案设计与 AI 辅助分析](https://github.com/OpenTenBase/OpenTenBase/issues/202)
> **认领者**: @muzimu217

---

## 一、测试目标

OpenTenBase 作为分布式 HTAP 数据库，性能测试不仅要关注单条 SQL 耗时，更要评估 **Coordinator 分发策略、DataNode 并行执行、GTM 全局事务管理、数据分布方式、并发连接** 等分布式特有因素对性能的影响。

本方案的目标是：

1. **量化各分布方式（Hash / Replication / Modulo / Shard）在不同查询模式下的性能特征**
2. **识别 CN → DN 分发策略（Remote SQL 下推 / 全 DN 广播 / 重分布）对延迟的影响**
3. **评估 GTM 在高并发事务场景下的吞吐瓶颈**
4. **为 OpenTenBase 生产部署提供可复现的性能基线参考**

---

## 二、测试环境规范

### 2.1 硬件环境

| 项目 | 最低配置 | 推荐配置 |
|------|----------|----------|
| CPU | 4 核 | 8 核+ |
| 内存 | 8 GB | 16 GB+ |
| 磁盘 | SSD 50 GB | SSD 200 GB+ |
| 网络 | 千兆内网 | 万兆内网 |

### 2.2 集群拓扑

**最小拓扑（单机测试）**：

| 角色 | 数量 | 端口 |
|------|------|------|
| GTM | 1 (主) | 6666 |
| Coordinator (CN) | 1 | 11000 |
| DataNode (DN) | 2 | 11008, 11009 |

**推荐拓扑（多机测试）**：

| 角色 | 数量 | 说明 |
|------|------|------|
| GTM | 1 主 + 1 从 | 全局事务管理 |
| CN | 2 | 查询分发与结果合并 |
| DN | 4 | 数据存储与并行执行 |

### 2.3 数据规模

| 表 | 分布方式 | 行数 | 说明 |
|------|----------|------|------|
| bench_hash_orders | HASH | 100,000 | 模拟订单主表 |
| bench_rep_customers | REPLICATION | 10,000 | 模拟小维度表 |
| bench_modulo_products | MODULO | 1,000 | 模拟均匀分布产品表 |
| bench_shard_transactions | SHARD | 100,000 | 模拟分片交易大表 |
| bench_hash_logs | HASH | 500,000 | 模拟高吞吐日志表 |

---

## 三、测试维度与脚本说明

### 3.1 测试维度总览

| 维度 | 测试目标 | 脚本 |
|------|----------|------|
| **单表写入** | INSERT TPS 与数据分布写入延迟 | `04_pgbench_scripts.sh` (write scripts) |
| **简单查询** | 分布键 vs 非分布键查询延迟差异 | `03_benchmark_queries.sql` + pgbench read |
| **聚合查询** | DN 本地聚合 → CN 合并的两阶段开销 | `03_benchmark_queries.sql` |
| **Join 查询** | Hash×Replication vs Hash×Hash vs 多表 Join | `03_benchmark_queries.sql` |
| **并发连接** | 多连接下 TPS/QPS 变化与 GTM 瓶颈 | `04_pgbench_scripts.sh` |
| **分布方式对比** | Hash/Replication/Modulo/Shard 的数据倾斜 | `05_distribution_analysis.sql` |

### 3.2 脚本使用步骤

```bash
# Step 1: 初始化 schema
psql -h <CN_IP> -p 11000 -U opentenbase -d postgres -f benchmark/01_schema_init.sql

# Step 2: 加载测试数据
psql -h <CN_IP> -p 11000 -U opentenbase -d postgres -f benchmark/02_data_load.sql

# Step 3: 执行基准查询 (EXPLAIN ANALYZE)
psql -h <CN_IP> -p 11000 -U opentenbase -d postgres -f benchmark/03_benchmark_queries.sql

# Step 4: 并发压力测试 (pgbench)
./benchmark/04_pgbench_scripts.sh <CN_IP> 11000 opentenbase

# Step 5: 分布式特征分析
psql -h <CN_IP> -p 11000 -U opentenbase -d postgres -f benchmark/05_distribution_analysis.sql

# Step 6: 清理 (可选)
psql -h <CN_IP> -p 11000 -U opentenbase -d postgres -f benchmark/06_cleanup.sql
```

---

## 四、关键观测指标

### 4.1 写入性能指标

| 指标 | 说明 | 采集方式 |
|------|------|----------|
| **INSERT TPS** | 每秒写入事务数 | pgbench 输出 |
| **写入延迟 (avg / p95 / p99)** | 单条 INSERT 的响应时间分布 | pgbench latency |
| **DN 分布写入延迟** | 不同 DN 的写入延迟差异 | EXPLAIN ANALYZE Remote SQL |
| **GTM 序列分配延迟** | GTM 分配全局序列号的开销 | pgxc_gtm_snap_stats |

### 4.2 查询性能指标

| 指标 | 说明 | 采集方式 |
|------|------|----------|
| **QPS** | 每秒查询数 | pgbench 输出 |
| **CN 分发延迟** | CN 解析 SQL → 下推到 DN 的网络延迟 | EXPLAIN ANALYZE |
| **DN 执行延迟** | DN 本地执行 SQL 的时间 | EXPLAIN ANALYZE |
| **CN 合并延迟** | DN 结果返回后 CN 合并排序的时间 | EXPLAIN ANALYZE |
| **重分布开销** | Join 时数据重分布的网络开销 | EXPLAIN ANALYZE |

### 4.3 分布式特征指标

| 指标 | 说明 | 采集方式 |
|------|------|----------|
| **数据倾斜率** | 各 DN 行数占比的最大偏差 | 05_distribution_analysis.sql |
| **查询计划类型** | Remote SQL / Broadcast / Redistribute / Local | EXPLAIN VERBOSE |
| **GTM 事务吞吐** | 全局事务分配速率 | pgxc_gtm_snap_stats |
| **并发 TPS 曲线** | 1/4/8/16/32 连接下 TPS 变化趋势 | pgbench 多连接测试 |

---

## 五、瓶颈分析方法

### 5.1 瓶颈来源判断矩阵

| 症状 | 可能瓶颈 | 验证方法 |
|------|----------|----------|
| 非分布键查询比分布键慢 5-10x | **CN 广播开销** | 对比 EXPLAIN 中 Remote SQL 的 Data Nodes 数量 |
| 并发增加但 TPS 不增加 | **GTM 瓶颈** | 查看 pgxc_gtm_snap_stats 的事务分配延迟 |
| GROUP BY 非分布键性能差 | **DN 重分布开销** | EXPLAIN 中出现 Redistribute Motion |
| 不同 DN 行数偏差 > 20% | **数据倾斜** | 05_distribution_analysis.sql 的倾斜分析 |
| JOIN 两 Hash 表慢 | **网络重分布** | EXPLAIN 中出现 Hash Join + Redistribute |
| 写入延迟随并发急剧上升 | **锁竞争 / 连接池** | 查看 CN/DN 的 pg_stat_activity |

### 5.2 OpenTenBase 特有优化点

| 场景 | 优化策略 |
|------|----------|
| 频繁查询小维度表 | 使用 Replication 分布 (CN 本地执行) |
| Join 大表与小维度表 | 小表用 Replication → Join 可 DN 本地执行 |
| 范围查询为主 | 使用 Shard / Range 分布替代 Hash |
| 高吞吐写入日志 | 使用 Hash 分布 + 批量 INSERT |
| 避免全 DN 广播 | 尽量在 WHERE 中使用分布键过滤 |

---

## 六、测试结果模板

运行完所有脚本后，将 `benchmark_results_*/results_summary_template.md` 中的空表填写为实际数据，并补充瓶颈分析。

---

## 七、AI 使用策略报告

### AI 工具使用说明

| 阶段 | AI 工具 | 使用方式 | AI 输出验证 |
|------|---------|----------|-------------|
| 方案设计 | WorkBuddy MVP 开发专家团 | 多角色协作：PM 分析需求 → 架构师设计测试框架 → 前端生成脚本 | 逐行审查 SQL 语法，对照 OpenTenBase DISTRIBUTE 语法验证 |
| SQL 脚本编写 | WorkBuddy (Craft 模式) | 生成 benchmark SQL 脚本 | 在 OpenTenBase 源码中交叉验证 LOCATOR_TYPE 定义、pgxc 模块兼容性 |
| pgbench 脚本 | WorkBuddy (Craft 模式) | 生成自定义 pgbench 测试脚本 | 对照 pgbench 官方文档验证脚本语法 |
| 分析框架 | WorkBuddy (Plan 模式) | 设计瓶颈判断矩阵 | 基于源码中的 pgxc/locator/planner 模块验证分析逻辑 |

### AI 输出审查与纠错

1. **SQL DISTRIBUTE 语法** — AI 初版使用了 `DISTRIBUTE BY` 语法，对照 OpenTenBase 源码 `src/include/pgxc/locator.h` 中 `LOCATOR_TYPE_*` 定义，确认 Hash/Replication/Modulo/Shard 均为合法分布方式
2. **pgbench 自定义脚本变量** — AI 生成的 `\set` 变量语法经过 `src/bin/pgbench/pgbench.h` 中的 `PgBenchFunction` 定义交叉验证
3. **查询计划分析** — 瓶颈判断矩阵基于 `src/backend/pgxc/plan/planner.c` 和 `src/backend/pgxc/locator/redistrib.c` 的实际分发逻辑设计，而非通用 PostgreSQL 知识
4. **拒绝的 AI 建议** — AI 曾建议使用 `DISTRIBUTE BY ROUND_ROBIN`，但源码中 LOCATOR_TYPE_RROBIN ('N') 已标记为遗留类型，改为使用 MODULO

---

## 八、声明

本测试方案的开发过程中使用了 WorkBuddy MVP 开发专家团（7 位专域角色 + SOP 流程）进行协作。善用 AI 专家团队可以显著缩短从需求理解到可交付方案的路径。
