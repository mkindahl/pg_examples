\echo Use "CREATE EXTENSION tasks" to load this file. \quit

-- Create the table of outstanding tasks to be executed.
create sequence @extschema@.task_id_seq as integer minvalue 1 cycle;

create table @extschema@.task (
    task_id integer not null default nextval('@extschema@.task_id_seq'::regclass),
    task_sched timestamptz,
    task_owner regrole,
    task_exec name,
    task_config jsonb,
    primary key (task_id)
);

alter sequence @extschema@.task_id_seq owned by @extschema@.task.task_id;

select pg_catalog.pg_extension_config_dump('@extschema@.task', '');

-- Function that returns the in-memory view of worker status
create function @extschema@.task_activity()
returns table(task_id, pid)
as 'MODULE_PATHNAME' language c;

create view @extschema@.task_stats(
    task_id, task_pid, task_sched, task_started, task_owner, task_exec, task_config
) as
select task_id, pid, task_sched, query_start, task_owner, task_exec, task_config
  from pg_stat_activity
  join task_activity() using (pid)
  join @extschema@.task using (task_id);

create procedure @extschema@.start_runners() as 'MODULE_PATHNAME', 'tasks_start' language c;
