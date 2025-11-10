-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION sorting" to load this file. \quit

-- Function to sort rows in a table using an index.
create function sorted(regclass, regclass) returns setof record
as '$libdir/functional', 'sorted_by_index' language c;
