-- Simple extension that contains a few tables and functions but
-- nothing else. We have a dedicated catalog schema for our internal
-- tables and functions.
--
-- The example is using an internal table to keep notes on existing
-- tables and it is extended to handle general metadata.

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION simple" to load this file. \quit

CREATE TABLE objects (
       objoid regclass,
       objnote text,
       primary key (objoid)
);

GRANT SELECT ON objects TO PUBLIC;

CREATE PROCEDURE public.add_note(regclass, text) LANGUAGE SQL AS $$
       INSERT INTO @extschema@.objects VALUES ($1, $2);
$$;

-- We want the contents of this table to be dumped by pg_dump
SELECT pg_catalog.pg_extension_config_dump('objects', '');
