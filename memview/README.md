# In-memory view of data

This is an example for how to create an in-memory view of "tabular"
data (table-like data, but you cannot define the table structure).

This approach is useful when you need to keep data updated for all
backends, and background workers, but the data is ephemeral.

We add functions to create an updatable view, which means we have
support for scanning the in-memory view, inserting new entries,
updating existing entries, and deleting existing entries.

A new view can be defined to call the `memview_view_scan` function
that will return a result set of rows with row identifier, database
OID, process identifier (just an arbitrary number for now), owner OID,
and description, in that order.

```sql
create view memview as
select row_id, owner::regrole, descr
  from memview_view_scan() v(row_id, dboid, owner, descr)
where pg_catalog.pg_has_role(current_user, record_owner, 'MEMBER'::text);
```

This defines a view with row-level security to prevent reading records
that you do not own. Since the shared memory is shared for all
databases, a database oid is added to each row to distinguish rows
from different databases.

This creates an insert, update, and delete triggers allowing you to
insert, update, or remove records to the memory view. 

```sql
create trigger memview_insert
   instead of insert on memview
   for each row execute function memview_insert_row_tgfunc(owner, descr);

create trigger memview_update
   instead of update on memview
   for each row execute function memview_update_row_tgfunc(row_id, owner, descr);

create trigger memview_delete
   instead of delete on memview
   for each row execute function memview_delete_row_tgfunc(row_id);
```

You need to pass in the column names of the memory view fields when
creating the trigger so that the trigger function knows which
attributes to use.
