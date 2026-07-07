-- verify_indexes.sql
-- Verify that the new indexes on opentenbase_subscription tables exist and
-- improve query performance.  Run after CREATE EXTENSION opentenbase_subscription.

-- 1. Confirm the indexes were created
SELECT indexname, indexdef
  FROM pg_indexes
 WHERE tablename IN ('opentenbase_subscription', 'opentenbase_subscription_parallel')
 ORDER BY tablename, indexname;

-- 2. Populate sample data so the planner has something to work with
INSERT INTO "opentenbase_subscription" ("sub_name", "sub_ignore_pk_conflict", "sub_parallel_number", "sub_is_all_actived")
SELECT ('sub_' || g)::name, false, 2, false
  FROM generate_series(1, 1000) g;

INSERT INTO "opentenbase_subscription_parallel" ("sub_parent", "sub_child", "sub_index", "sub_active_state", "sub_active_lsn")
SELECT o.oid, (10000 + row_number() OVER ())::oid, (row_number() OVER (PARTITION BY o.oid))::int4, false, '0/0'::pg_lsn
  FROM "opentenbase_subscription" o,
       generate_series(1, 2) s;

ANALYZE "opentenbase_subscription";
ANALYZE "opentenbase_subscription_parallel";

-- 3. Show EXPLAIN plans that should use Index Scan / Index Only Scan

-- 3a. Lookup subscription by sub_name (used in subscriptioncmds.c existence check)
EXPLAIN (COSTS OFF)
SELECT * FROM "opentenbase_subscription" WHERE "sub_name" = 'sub_500';

-- 3b. Lookup parallel entry by sub_child (used in pg_subscription.c to find parent)
EXPLAIN (COSTS OFF)
SELECT * FROM "opentenbase_subscription_parallel" WHERE "sub_child" = 10500;

-- 3c. Lookup parallel entries by sub_parent (used for cascade delete and activation)
EXPLAIN (COSTS OFF)
SELECT * FROM "opentenbase_subscription_parallel" WHERE "sub_parent" = (
    SELECT oid FROM "opentenbase_subscription" WHERE "sub_name" = 'sub_1' LIMIT 1
);

-- 4. Clean up sample data
DELETE FROM "opentenbase_subscription_parallel";
DELETE FROM "opentenbase_subscription";
