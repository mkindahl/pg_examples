-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION aggs" to load this file. \quit

CREATE FUNCTION window_agg_dropfn(internal, anynonarray)
       RETURNS internal
       AS '$libdir/aggs.so' LANGUAGE C;
CREATE FUNCTION window_agg_transfn(internal, anynonarray)
       RETURNS internal
       AS '$libdir/aggs.so' LANGUAGE C;
CREATE FUNCTION window_agg_finalfn(internal, anynonarray)
       RETURNS anyarray
       AS '$libdir/aggs.so' LANGUAGE C;

CREATE FUNCTION window_agg_dropfn(internal, anynonarray, integer)
       RETURNS internal
       AS '$libdir/aggs.so' LANGUAGE C;
CREATE FUNCTION window_agg_transfn(internal, anynonarray, integer)
       RETURNS internal
       AS '$libdir/aggs.so' LANGUAGE C;
CREATE FUNCTION window_agg_finalfn(internal, anynonarray, integer)
       RETURNS anyarray
       AS '$libdir/aggs.so' LANGUAGE C;

CREATE AGGREGATE window_agg(anynonarray, integer)
(
    sfunc = window_agg_transfn,
    stype = internal,
    finalfunc = window_agg_finalfn,
    msfunc = window_agg_transfn,
    minvfunc = window_agg_dropfn,
    mfinalfunc = window_agg_finalfn,
    mstype = internal,
    finalfunc_extra,
    mfinalfunc_extra
);
