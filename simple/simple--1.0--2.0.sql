-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION simple" to load this file. \quit

-- Here we re-create the object entirely, but copy the old data from the table.

CREATE TABLE tmp_objects (LIKE objects);
INSERT INTO tmp_objects (SELECT * FROM objects);

DROP TABLE objects;

CREATE TABLE objects (
       objoid regclass,
       objnote json,
       primary key (objoid)
);

GRANT SELECT ON objects TO PUBLIC;

INSERT INTO objects (SELECT objoid, json_build_object('note', objnote) FROM tmp_objects);

CREATE OR REPLACE PROCEDURE public.add_note(regclass, text) LANGUAGE SQL AS $$
       INSERT INTO @extschema@.objects VALUES ($1, json_build_object('note', $2));
$$;

DROP TABLE tmp_objects;
