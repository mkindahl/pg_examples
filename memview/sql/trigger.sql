create role wizard;
create role unicorn;

create view memview as
select row_id, owner::regrole, descr
from memview_view_scan() v(row_id, dboid, owner, descr)
where dboid = (select oid from pg_database where datname = current_database());

create trigger memview_insert
   instead of insert on memview
   for each row
   execute function memview_insert_row_tgfunc(owner, descr);

select owner, descr from memview;
insert into memview(owner, descr) values ('wizard'::regrole, 'with magic');
insert into memview(owner, descr) values ('wizard'::regrole, 'more magic');
select owner, descr from memview;

create trigger memview_delete
   instead of delete on memview
   for each row
   execute function memview_delete_row_tgfunc(row_id);

select owner, descr from memview;
delete from memview where descr = 'with magic';
select owner, descr from memview;
delete from memview where descr = 'no magic';
select owner, descr from memview;

create trigger memview_update
   instead of update on memview
   for each row
   execute function memview_update_row_tgfunc(row_id, owner, descr);

select owner, descr from memview;
update memview set owner = 'unicorn' where descr = 'more magic';
select owner, descr from memview;
update memview set owner = 'unicorn' where descr = 'with magic';
select owner, descr from memview;

-- Updating the row id should not be possible so check that.
\set ON_ERRROR_STOP 0
select row_id from memview where owner = 'unicorn'::regrole \gset
update memview set row_id = row_id + 1 where row_id = :row_id;
\set ON_ERRROR_STOP 1

drop view memview;
drop role wizard;
drop role unicorn;
