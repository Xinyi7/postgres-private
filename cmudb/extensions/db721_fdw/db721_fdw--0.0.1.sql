\echo Use "CREATE EXTENSION db721_fdw" to load this file. \quit

CREATE FUNCTION db721_fdw_handler()
    RETURNS fdw_handler
    AS 'MODULE_PATHNAME'
    LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER db721_fdw
    HANDLER db721_fdw_handler;