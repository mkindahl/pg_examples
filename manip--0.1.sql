-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION manip" to load this file. \quit

CREATE PROCEDURE scan_table(relation regclass) LANGUAGE C AS '$libdir/manip.so';
