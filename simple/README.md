# Demonstrate upgrade problem

This is posted as [Bug in handling default privileges inside extension
update scripts][1] to [pgsql-bugs][2].

If you modify grants inside an extension and the user is granting
default privileges, this is a problem when dumping the database using
`pg_dump`.

The problem occurs when the user grants privileges on the
configuration tables for the extension to a new role and want to set
default privileges for the role on both the current and future
configuration tables.

As a simple concrete example, suppose that the user want to add a
dedicated role (say `reader`) for backing up the database using
`pg_dump`, with all configuration tables of the extension(s) as
well. I would argue that a user can expect the following:

* To be able to read the configuration tables, `reader` need to have
  SELECT privileges.

* Since the new role is added by the user and not by the extension,
  the grants have to be dumped as well. Otherwise, a restore of the
  data will have wrong privileges.

* Since new configuration tables could be added by an update of the
  extension, it is necessary to make sure that these privileges are
  added to new tables when updating. Typically, this means changing
  the default privileges on the schema for the configuration files.

* After an update, the new role with privileges should still be dumped
  both for old tables as well as for new tables. Note that some "old"
  tables could have changed definition in the update (typically by
  creating the new version and copying data from the old table), but
  these should still keep the same privileges and also be dumped
  after the update.

The problem can be reproduced using this extension in the following manner:

    $ psql
    Expanded display is used automatically.
    Null display is "[NULL]".
    psql (12.6 (Ubuntu 12.6-0ubuntu0.20.04.1))
    Type "help" for help.

    mats=# CREATE SCHEMA IF NOT EXISTS simple_catalog;
    CREATE SCHEMA
    mats=# CREATE EXTENSION simple WITH VERSION '1.0' SCHEMA simple_catalog;
    CREATE EXTENSION

We add a new role that should be able to read all the configuration
tables of the extension.

    mats=# CREATE ROLE reader;
    CREATE ROLE

Since these needs to be possible to dump, we need to grant SELECT
privileges on the tables in the schema.

    mats=# GRANT SELECT ON ALL TABLES IN SCHEMA simple_catalog TO reader;
    GRANT

Since an update can create new tables, and we want the "reader" role
to still be able to read those tables after an update, we grant
default privileges on the schema to "reader".

    mats=# ALTER DEFAULT PRIVILEGES IN SCHEMA simple_catalog GRANT SELECT ON TABLES TO reader;
    ALTER DEFAULT PRIVILEGES

Note that at this point, we have these ACLs and initprivs.

    mats=# SELECT nspname, relname, relacl, initprivs
    mats-# FROM pg_class cl JOIN pg_namespace ns ON ns.oid = relnamespace LEFT JOIN pg_init_privs ON objoid = cl.oid
    mats-# WHERE relname = 'objects';
        nspname     | relname |                  relacl                   |          initprivs
    ----------------+---------+-------------------------------------------+-----------------------------
     simple_catalog | objects | {mats=arwdDxt/mats,=r/mats,reader=r/mats} | {mats=arwdDxt/mats,=r/mats}
    (1 row)

We now add some configuration data to the configuration schema of the
extension so that we can see that it is dumped correctly.

    mats=# CREATE TABLE update_foo (a int);
    CREATE TABLE
    mats=# CALL add_note('update_foo', 'Just a note');
    CALL
    mats=# SELECT * FROM simple_catalog.objects;
       objoid   |   objnote  
    ------------+-------------
     update_foo | Just a note
    (1 row)

Running "pg_dump" and checking for grants for "reader". We expect it
to dump the grants here since the user created those and not the
extension. And this does indeed work as expected: both grants and
default privileges are dumped.

    $ pg_dump | grep reader
    GRANT SELECT ON TABLE simple_catalog.objects TO reader;
    ALTER DEFAULT PRIVILEGES FOR ROLE mats IN SCHEMA simple_catalog GRANT SELECT ON TABLES  TO reader;

Updating the extension adds the default privileges to "initprivs",
which is unexpected.

    mats=# ALTER EXTENSION simple UPDATE;
    ALTER EXTENSION
    mats=# SELECT nspname, relname, relacl, initprivs
    mats-# FROM pg_class cl JOIN pg_namespace ns ON ns.oid = relnamespace LEFT JOIN pg_init_privs ON objoid = cl.oid
    mats-# WHERE relname = 'objects';
        nspname     | relname |                  relacl                   |                initprivs                
    ----------------+---------+-------------------------------------------+-------------------------------------------
     simple_catalog | objects | {mats=arwdDxt/mats,reader=r/mats,=r/mats} | {mats=arwdDxt/mats,reader=r/mats,=r/mats}
    (1 row)

As a result, this means that when we dump the database, it will not
dump grants for "reader" even though those were given by the user and
not the extension and, IMHO, after the update it should not behave
differently compared to before the update.

    $ pg_dump | grep reader
    ALTER DEFAULT PRIVILEGES FOR ROLE mats IN SCHEMA simple_catalog GRANT SELECT ON TABLES TO reader;

It is possible to solve this in the extension by saving away all
privileges and initprivs for the schema and restoring them afterwards,
which is quite cumbersome.

A better approach (IMHO) would be to make sure that new tables do
not use the default privileges to change the initprivs when doing the
extension update. This would allow extensions to add grants as they
please and it will still behave as expected (these will not be dumped).

[1]: https://www.postgresql.org/message-id/CA%2B144272PPX%2By7L2V6DiYsv8pLhKrmpLmaZJnka2CKrXLnfY_A%40mail.gmail.com
[2]: https://www.postgresql.org/list/pgsql-bugs/
