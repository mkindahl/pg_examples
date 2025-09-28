-- Simple function that executes something.
create function do_something(timestamptz, jsonb) returns void as
$$
begin
  perform pg_sleep(10);
  raise log 'executed task scheduled at % with configuration %', $1, $2;
end
$$ language plpgsql;

-- Insert a list of tasks to execute with a random scheduled time.
insert into tasks.task(task_sched, task_owner, task_exec, task_config)
select now() + random() * '1 minute'::interval,
       current_user::regrole,
       'do_something',
       format('{"foo": %s}', n)::jsonb
from generate_series(1,100) n;
