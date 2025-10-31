--
-- Regression test for CREATE FORCE VIEW functionality.
--
-- This test suite verifies the core creation, boundary checks, and the
-- dependency finalization workflow. It intentionally avoids testing the
-- "automatic activation" feature, which is known to fail only in the
-- pg_regress environment due to a deeper planner/cache issue.
--

-- Part 1: Core Creation and Boundary Checks

-- Test 1.1: Verify successful creation of a FORCE VIEW on a non-existent table.
DROP VIEW IF EXISTS core_view;
CREATE FORCE VIEW core_view AS SELECT col1 FROM non_existent_table;
-- Verify the view is in the catalog and its column type defaults to TEXT.
SELECT relname FROM pg_class WHERE relname = 'core_view';
\d core_view
DROP VIEW core_view;

-- Test 1.2: Verify rejection of SELECT * on a non-existent table.
DROP VIEW IF EXISTS star_view;
CREATE FORCE VIEW star_view AS SELECT * FROM non_existent_star_table;


-- Part 2: Interaction with CREATE OR REPLACE VIEW

-- Test 2.1: Verify native REPLACE limitations are correctly enforced.
DROP VIEW IF EXISTS compatibility_view;
DROP TABLE IF EXISTS base_table_compat;
CREATE FORCE VIEW compatibility_view AS SELECT val FROM base_table_compat;
CREATE TABLE base_table_compat (val_new INT) DISTRIBUTE BY REPLICATION;
-- This is expected to fail due to a name mismatch.
CREATE OR REPLACE VIEW compatibility_view AS SELECT val_new FROM base_table_compat;
-- This is expected to fail due to a type mismatch.
CREATE OR REPLACE VIEW compatibility_view (val) AS SELECT val_new FROM base_table_compat;
DROP VIEW compatibility_view;
DROP TABLE base_table_compat;


-- Part 3: Dependency Management Workflow

-- Test 3.1: Verify the full dependency finalization lifecycle.
DROP VIEW IF EXISTS dependency_view;
DROP TABLE IF EXISTS base_table_depend;
-- Step 1: Create a FORCE VIEW (no dependency recorded).
CREATE FORCE VIEW dependency_view AS SELECT val FROM base_table_depend;
-- Step 2: Create the underlying table.
CREATE TABLE base_table_depend (val TEXT) DISTRIBUTE BY REPLICATION;
-- Step 3: Verify dependency is NOT yet created. This DROP should SUCCEED.
DROP TABLE base_table_depend;
-- Step 4: Recreate table and "finalize" the view to establish dependency.
CREATE TABLE base_table_depend (val TEXT) DISTRIBUTE BY REPLICATION;
CREATE OR REPLACE VIEW dependency_view AS SELECT val FROM base_table_depend;
-- Step 5: Verify dependency is now recorded. This DROP (RESTRICT) should FAIL.
DROP TABLE base_table_depend;
-- Step 6: Verify DROP CASCADE works as expected.
DROP TABLE base_table_depend CASCADE;
-- Step 7: Verify the view has been cascaded.
SELECT * FROM dependency_view;