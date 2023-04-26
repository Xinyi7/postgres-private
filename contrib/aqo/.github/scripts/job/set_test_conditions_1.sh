#!/bin/bash

# ##############################################################################
#
# Test conditions No.1: Quick pass in 'disabled' mode with statistics and
# forced usage of a bunch of parallel workers.
#
# - Disabled mode with a stat gathering and AQO details in explain
# - Force usage of parallel workers aggressively
# - Enable pg_stat_statements statistics
#
# ##############################################################################

# AQO specific settings
psql -c "ALTER SYSTEM SET aqo.mode = 'disabled'"
psql -c "ALTER SYSTEM SET aqo.force_collect_stat = 'on'"
psql -c "ALTER SYSTEM SET aqo.show_details = 'on'"
psql -c "ALTER SYSTEM SET aqo.show_hash = 'on'"

# Core settings: force parallel workers
psql -c "ALTER SYSTEM SET max_parallel_workers_per_gather = 16"
psql -c "ALTER SYSTEM SET force_parallel_mode = 'on'"
psql -c "ALTER SYSTEM SET from_collapse_limit = 20"
psql -c "ALTER SYSTEM SET join_collapse_limit = 20"
psql -c "ALTER SYSTEM SET parallel_setup_cost = 1.0"
psql -c "ALTER SYSTEM SET parallel_tuple_cost = 0.00001"
psql -c "ALTER SYSTEM SET min_parallel_table_scan_size = 0"
psql -c "ALTER SYSTEM SET min_parallel_index_scan_size = 0"

# pg_stat_statements
psql -c "ALTER SYSTEM SET pg_stat_statements.track = 'all'"
psql -c "ALTER SYSTEM SET pg_stat_statements.track_planning = 'on'"

psql -c "SELECT pg_reload_conf();"

# Enable all previously executed queries which could be disabled
psql -c "
  SELECT count(*) FROM aqo_queries, LATERAL aqo_disable_class(queryid)
  WHERE queryid <> 0
"

