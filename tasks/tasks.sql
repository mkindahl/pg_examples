\echo Use "CREATE EXTENSION tasks" to load this file. \quit

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

create procedure @extschema@.start_runners() as 'MODULE_PATHNAME', 'tasks_start' language c;
