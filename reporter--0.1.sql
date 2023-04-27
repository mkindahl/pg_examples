-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION worker_spi" to load this file. \quit

CREATE FUNCTION generate_report() RETURNS jsonb AS $$
  SELECT json_build_object('tables', count(*)) FROM pg_class WHERE reltype != 0;
$$ LANGUAGE SQL;
