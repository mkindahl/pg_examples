CREATE SCHEMA IF NOT EXISTS simple_catalog;
CREATE EXTENSION simple WITH VERSION '1.0' SCHEMA simple_catalog;

CREATE ROLE reader;

GRANT SELECT ON ALL TABLES IN SCHEMA simple_catalog TO reader;
ALTER DEFAULT PRIVILEGES IN SCHEMA simple_catalog GRANT SELECT ON TABLES TO reader;

SELECT nspname, defaclacl FROM pg_namespace ns JOIN pg_default_acl ON ns.oid = defaclnamespace;

WITH
  summary AS (
    SELECT nspname,
           relname,
           (aclexplode(relacl)).privilege_type AS relacl_privilege_type,
           (aclexplode(relacl)).grantee::regrole AS relacl_grantee,
           privtype,
           (aclexplode(initprivs)).privilege_type AS initprivs_privilege_type,
           (aclexplode(initprivs)).grantee::regrole AS initprivs_grantee
      FROM pg_class cl
      JOIN pg_namespace ns ON ns.oid = relnamespace
 LEFT JOIN pg_init_privs ON objoid = cl.oid
     WHERE relname = 'objects')
SELECT * INTO saved FROM summary;

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

WITH
  summary AS (
    SELECT nspname,
           relname,
           (aclexplode(relacl)).privilege_type AS relacl_privilege_type,
           (aclexplode(relacl)).grantee::regrole AS relacl_grantee,
           privtype,
           (aclexplode(initprivs)).privilege_type AS initprivs_privilege_type,
           (aclexplode(initprivs)).grantee::regrole AS initprivs_grantee
      FROM pg_class cl
      JOIN pg_namespace ns ON ns.oid = relnamespace
 LEFT JOIN pg_init_privs ON objoid = cl.oid
     WHERE relname = 'objects')
SELECT COALESCE(saved.nspname, summary.nspname) AS nspname,
       COALESCE(saved.relname, summary.relname) AS relname,
       COALESCE(saved.privtype, summary.privtype) AS privtype,
       saved.relacl_privilege_type, summary.relacl_privilege_type,
       saved.relacl_grantee, summary.relacl_grantee,
       saved.initprivs_privilege_type, summary.initprivs_privilege_type,
       saved.initprivs_grantee, summary.initprivs_grantee
  FROM summary FULL JOIN saved ON row(saved.*) = row(summary.*)
 WHERE saved.relname IS NULL OR summary.relname IS NULL;

DROP EXTENSION simple;
DROP SCHEMA simple_catalog;
DROP TABLE update_foo;
DROP ROLE reader;
