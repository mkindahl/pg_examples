CREATE TYPE tagged;

CREATE FUNCTION tagged_in(cstring) RETURNS tagged STRICT IMMUTABLE LANGUAGE C AS '$libdir/tagged.so';
CREATE FUNCTION tagged_out(tagged) RETURNS cstring STRICT IMMUTABLE LANGUAGE C AS '$libdir/tagged.so';

CREATE TYPE tagged (
    INPUT = tagged_in,
    OUTPUT = tagged_out,
    INTERNALLENGTH = VARIABLE
);

CREATE FUNCTION hash_part(col name, partitions int) RETURNS tagged LANGUAGE C as '$libdir/tagged.so';
CREATE FUNCTION range_part(col name, ivl interval) RETURNS tagged LANGUAGE C as '$libdir/tagged.so';

CREATE PROCEDURE builder(rel regclass, cols anyarray) LANGUAGE C AS '$libdir/tagged.so', 'builder_array';
CREATE PROCEDURE builder(rel regclass, cols anynonarray) LANGUAGE C AS '$libdir/tagged.so', 'builder_elem';
