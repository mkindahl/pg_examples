CREATE EXTENSION memview;

CREATE ROLE wizard;

CREATE VIEW memview AS
SELECT row_id, pid, owner::regrole, descr
FROM memview_view_scan() v(row_id, dboid, pid, owner, descr)
WHERE dboid = (select oid from pg_database where datname = current_database());

-- Test insert function.
SELECT pid, owner, descr FROM memview;
SELECT memview_row_insert(1, 'wizard'::regrole, 'With magic');
SELECT pid, owner, descr FROM memview;
SELECT memview_row_insert(2, 'wizard'::regrole, 'More magic');
SELECT pid, owner, descr FROM memview;

-- Test update function.
SELECT row_id from memview where descr = 'With magic' limit 1 \gset
SELECT memview_row_update(:row_id, 3, 'wizard'::regrole, 'Less magic');
SELECT pid, owner, descr FROM memview;

-- Test delete function. Note that the row id changes since the table
-- is compacted.
SELECT memview_row_delete(:row_id);
SELECT pid, owner, descr FROM memview;

SELECT memview_row_delete(row_id) FROM memview;

DROP ROLE wizard;

