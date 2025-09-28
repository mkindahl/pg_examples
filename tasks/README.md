# Task Runner Pool with Task Queue

A simple example of how you can implement a pool of workers that runs
continuously and reads tasks to be done from a task queue.

## Build and installation

To build and install the shared libraries and installation files:

```bash
make
make install
```

You can then install the extension in a database using the command:

```sql
create extension tasks;
```

## Starting worker pool

The number of workers in the worker pool is controlled using
`tasks.workers` and defaults to 4 workers. This is a configuration
option that can be set in the configuration file and requires a
restart to take effect.

To start the worker pool in a database, install the extension and use
the procedure `tasks.start_runners`. These runners will run as the
user starting the runners, so you need to ensure that you have access
permissions to the `tasks.task` table.

```sql
call tasks.start_runners();
```

If you want a worker pool to be started at cluster startup you need to
add the `tasks` extension to the `shared_preload_libraries` option and
add the database to `tasks.databases`. In this case the runners will
run without permissions (no user at all) and have full access to the
database.

```
shared_preload_libraries = 'tasks'
tasks.databases = 'mydb,otherdb'
```

In the event that the databases do not exist when you start the
cluster, the workers will attempt to reconnect until the database is
created and the extension loaded for the database.

## Configuration parameters

`tasks.workers`
: Number of workers registered when calling `tasks.start_runners` or
  when starting the cluster.

`tasks.nap_time`
: When tasks are not found in the task queue, it will nap this many
  seconds before checking again. It defaults to 1 second.
  
`tasks.restart_time`
: On error causing an exit code of 1, workers will restart after these
  many seconds.
