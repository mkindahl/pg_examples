CREATE SCHEMA simple_catalog;
CREATE EXTENSION simple WITH SCHEMA simple_catalog;
CREATE TABLE basic_foo (a int);

CALL add_note('basic_foo', 'Just a note');

\d
\d simple_catalog.*

SELECT * FROM simple_catalog.objects;

DROP EXTENSION simple;
DROP SCHEMA simple_catalog;
DROP TABLE basic_foo;

