--
-- Regression test for CREATE FORCE VIEW functionality (Stable Version)
--

-- ========== Test Case 1: Core CREATE FORCE VIEW Functionality ==========
\echo 'Test Case 1: Verify successful creation of a FORCE VIEW on a non-existent table.'
DROP VIEW IF EXISTS core_view;
CREATE FORCE VIEW core_view AS SELECT col1 FROM non_existent_table;
-- Verify the view is in the catalog
SELECT relname FROM pg_class WHERE relname = 'core_view';
\d core_view
DROP VIEW core_view;


-- ========== Test Case 2: Boundary Condition - SELECT * ==========
\echo 'Test Case 2: Verify rejection of SELECT * on a non-existent table.'
DROP VIEW IF EXISTS star_test_view;
CREATE FORCE VIEW star_test_view AS SELECT * FROM non_existent_star_table;


-- ========== Test Case 3: NORMAL to FORCE VIEW Transition ==========
-- This test verifies that CREATE OR REPLACE FORCE VIEW can successfully
-- replace an existing NORMAL view, even if column names and types change,
-- which relies on the 'checkViewTupleDesc' patch.
\echo 'Test Case 3: Verify transition from a NORMAL to a FORCE view.'
DROP VIEW IF EXISTS transition_view;
DROP TABLE IF EXISTS base_table_c;
CREATE TABLE base_table_c (val_c TEXT) DISTRIBUTE BY REPLICATION;
CREATE VIEW transition_view AS SELECT * FROM base_table_c;
-- This should succeed because 'checkViewTupleDesc' is patched to allow it
CREATE OR REPLACE FORCE VIEW transition_view AS SELECT col_x FROM non_existent_table_c;
\d transition_view
DROP VIEW transition_view;
DROP TABLE base_table_c;