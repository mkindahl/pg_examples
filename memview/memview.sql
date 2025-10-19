-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION memview" to load this file. \quit

create function memview_view_scan()
returns table(row_id integer, dboid Oid, owner oid, description name)
as 'memview' language c;

create procedure memview_view_reset() as 'memview' language c;

create function memview_row_insert(owner oid, description name)
    returns void as 'memview' language c;

create function memview_row_update(row_id integer, owner oid, description name)
    returns void as 'memview' language c;

create function memview_row_delete(row_id integer)
    returns void as 'memview' language c;

create function memview_insert_row_tgfunc() returns trigger as 'memview' language c;
create function memview_delete_row_tgfunc() returns trigger as 'memview' language c;
create function memview_update_row_tgfunc() returns trigger as 'memview' language c;
