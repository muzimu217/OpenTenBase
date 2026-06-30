#!/bin/bash
# OpenTenBase K8s 初始化脚本（Operator sidekick init-container 模板）
# 顺序：GTM → DN → CN
# 此脚本作为 Operator 控制器生成的 init-container 入口点

set -euo pipefail

ROLE="${ROLE:-unknown}"  # gtm-master | gtm-standby | coordinator | datanode
CLUSTER_NAME="${CLUSTER_NAME:-demo-cluster}"
NAMESPACE="${NAMESPACE:-opentenbase}"

# ── 通用工具函数 ──
log() { echo "[init] $(date '+%H:%M:%S') $*"; }
wait_for_service() {
  local host="$1" port="$2" max="${3:-60}"
  log "等待 $host:$port 可达 ..."
  for i in $(seq 1 "$max"); do
    if nc -z "$host" "$port" 2>/dev/null; then
      log "$host:$port 已就绪 (${i}s)"
      return 0
    fi
    sleep 1
  done
  log "超时: $host:$port 不可达 (${max}s)"
  return 1
}

# ── GTM 初始化 ──
init_gtm() {
  log "初始化 GTM Master ..."
  # GTM 不依赖其他组件，直接启动
  if [ ! -f "$PGDATA/gtm.control" ]; then
    initdb -D "$PGDATA" --no-locale --encoding=UTF8
    # 写入 gtm.conf
    cat > "$PGDATA/gtm.conf" <<EOF
listen_addresses = '*'
port = 6666
gtm_standby_mode = off
EOF
    log "GTM initdb 完成"
  else
    log "GTM 数据目录已存在，跳过 initdb"
  fi
}

init_gtm_standby() {
  log "初始化 GTM Standby ..."
  # 等待 GTM Master 就绪
  wait_for_service "${CLUSTER_NAME}-gtm-master" 6666
  # 从 GTM Master 同步数据
  if [ ! -f "$PGDATA/gtm.control" ]; then
    pg_basebackup -h "${CLUSTER_NAME}-gtm-master" -p 6666 -D "$PGDATA" -X stream -P
    # 修改 gtm.conf 为 standby 模式
    sed -i 's/gtm_standby_mode = off/gtm_standby_mode = on/' "$PGDATA/gtm.conf"
    echo "active_host = '${CLUSTER_NAME}-gtm-master'" >> "$PGDATA/gtm.conf"
    echo "active_port = 6666" >> "$PGDATA/gtm.conf"
    log "GTM Standby 同步完成"
  fi
}

# ── DataNode 初始化 ──
init_datanode() {
  local dn_id="${DN_ID:-1}"
  log "初始化 DataNode $dn_id ..."

  # 等待 GTM 就绪
  wait_for_service "${CLUSTER_NAME}-gtm-master" 6666

  if [ ! -f "$PGDATA/PG_VERSION" ]; then
    initdb -D "$PGDATA" --no-locale --encoding=UTF8
    # 写入 postgresql.conf（DN 模式）
    cat >> "$PGDATA/postgresql.conf" <<EOF
# OpenTenBase DataNode 配置
pgxc_node_name = 'dn${dn_id}'
gtm_host = '${CLUSTER_NAME}-gtm-master'
gtm_port = 6666
listen_addresses = '*'
port = 5432
EOF
    log "DN initdb 完成"
  fi

  # 启动临时 PostgreSQL 用于节点注册
  pg_ctl -D "$PGDATA" -l "$PGDATA/logfile" start -w
  sleep 2

  # 注册 GTM 连接
  psql -d postgres -c "ALTER NODE dn${dn_id} WITH (TYPE='datanode', HOST='${CLUSTER_NAME}-dn${dn_id}', PORT=5432);" 2>/dev/null || true

  pg_ctl -D "$PGDATA" stop -w
  log "DataNode $dn_id 初始化完成"
}

# ── Coordinator 初始化 ──
init_coordinator() {
  local cn_id="${CN_ID:-1}"
  log "初始化 Coordinator $cn_id ..."

  # 等待 GTM + 所有 DataNode 就绪
  wait_for_service "${CLUSTER_NAME}-gtm-master" 6666
  for i in $(seq 1 "${DN_COUNT:-2}"); do
    wait_for_service "${CLUSTER_NAME}-dn${i}" 5432
  done

  if [ ! -f "$PGDATA/PG_VERSION" ]; then
    initdb -D "$PGDATA" --no-locale --encoding=UTF8
    # 写入 postgresql.conf（CN 模式）
    cat >> "$PGDATA/postgresql.conf" <<EOF
# OpenTenBase Coordinator 配置
pgxc_node_name = 'cn${cn_id}'
gtm_host = '${CLUSTER_NAME}-gtm-master'
gtm_port = 6666
listen_addresses = '*'
port = 5432
EOF
    log "CN initdb 完成"
  fi

  # 启动临时 PostgreSQL 用于节点注册
  pg_ctl -D "$PGDATA" -l "$PGDATA/logfile" start -w
  sleep 2

  # 注册 GTM
  psql -d postgres -c "CREATE NODE cn${cn_id} WITH (TYPE='coordinator', HOST='${CLUSTER_NAME}-cn${cn_id}', PORT=5432, PRIMARY=true);" 2>/dev/null || true

  # 注册所有 DataNode
  for i in $(seq 1 "${DN_COUNT:-2}"); do
    psql -d postgres -c "CREATE NODE dn${i} WITH (TYPE='datanode', HOST='${CLUSTER_NAME}-dn${i}', PORT=5432, PRIMARY=true);" 2>/dev/null || \
    psql -d postgres -c "ALTER NODE dn${i} WITH (HOST='${CLUSTER_NAME}-dn${i}', PORT=5432);" 2>/dev/null || true
  done

  pg_ctl -D "$PGDATA" stop -w
  log "Coordinator $cn_id 初始化完成"
}

# ── 主入口 ──
case "$ROLE" in
  gtm-master)    init_gtm ;;
  gtm-standby)   init_gtm_standby ;;
  datanode)      init_datanode ;;
  coordinator)   init_coordinator ;;
  *)             log "未知角色: $ROLE"; exit 1 ;;
esac

log "初始化完成，等待 Operator 启动主进程"
