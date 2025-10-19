create role wizard;

create view memview as
select row_id, owner::regrole, descr
from memview_view_scan() v(row_id, dboid, owner, descr)
where dboid = (select oid from pg_database where datname = current_database());

-- Test insert function.
select owner, descr from memview;
select memview_row_insert('wizard'::regrole, 'with magic');
select owner, descr from memview;
select memview_row_insert('wizard'::regrole, 'more magic');
select owner, descr from memview;

-- Test update function.
select row_id from memview where descr = 'with magic' limit 1 \gset
select memview_row_update(:row_id, 'wizard'::regrole, 'less magic');
select owner, descr from memview;

-- Test delete function. Note that the row id changes since the table
-- is compacted.
select memview_row_delete(:row_id);
select owner, descr from memview;

select memview_row_delete(row_id) from memview;
drop view memview;
drop role wizard;

