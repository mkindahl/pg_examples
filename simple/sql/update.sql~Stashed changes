CREATE SCHEMA IF NOT EXISTS simple_catalog;
CREATE EXTENSION simple WITH VERSION '1.0' SCHEMA simple_catalog;

CREATE ROLE reader;

GRANT SELECT ON ALL TABLES IN SCHEMA simple_catalog TO reader;
ALTER DEFAULT PRIVILEGES IN SCHEMA simple_catalog GRANT SELECT ON TABLES TO reader;

SELECT nspname, defaclacl FROM pg_namespace ns JOIN pg_default_acl ON ns.oid = defaclnamespace;

SELECT nspname, relname, relacl, privtype, initprivs
FROM pg_class cl JOIN pg_namespace ns ON ns.oid = relnamespace LEFT JOIN pg_init_privs ON objoid = cl.oid
WHERE relname = 'objects';

SELECT nspname, nspacl, privtype, initprivs
FROM pg_namespace ns LEFT JOIN pg_init_privs ON objoid = ns.oid
WHERE nspname = 'simple_catalog';

CREATE TABLE update_foo (a int);
CALL add_note('update_foo', 'Just a note');
SELECT * FROM simple_catalog.objects;
ALTER EXTENSION simple UPDATE;
SELECT * FROM simple_catalog.objects;

SELECT nspname, nspacl, privtype, initprivs
FROM pg_namespace ns LEFT JOIN pg_init_privs ON objoid = ns.oid
WHERE nspname = 'simple_catalog';

SELECT nspname, relname, relacl, privtype, initprivs
FROM pg_class cl JOIN pg_namespace ns ON ns.oid = relnamespace LEFT JOIN pg_init_privs ON objoid = cl.oid
WHERE relname = 'objects';

DROP EXTENSION simple;
DROP SCHEMA simple_catalog;
DROP TABLE update_foo;
DROP ROLE reader;
