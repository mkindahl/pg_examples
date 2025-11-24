/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 */
#pragma once

#include <postgres.h>
#include <fmgr.h>

#include <datatype/timestamp.h>
#include <executor/spi.h>
#include <postmaster/bgworker.h>

#if PG_VERSION_NUM < 180000
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define pg_noreturn _Noreturn
#elif defined(__GNUC__) || defined(__SUNPRO_C)
#define pg_noreturn __attribute__((noreturn))
#elif defined(_MSC_VER)
#define pg_noreturn __declspec(noreturn)
#else
#define pg_noreturn
#endif
#endif

#define TASK_RUNNER_MAGIC 0xdeadbeef /* Temporary magic number */

/*
 * Task runner arguments passed down through bgw_extra.
 *
 * Depending on the value of roleoid, use either the database name or
 * database OID for worker. If roleoid is InvalidOid, use the dbname,
 * otherwise, use dboid.
 */
typedef struct TaskRunnerArgs {
  Oid roleoid;
  union {
    char dbname[BGW_EXTRALEN - sizeof(Oid) - 1];
    Oid dboid;
  };
} TaskRunnerArgs;

typedef struct TaskRunnerState {
  TimestampTz next_wakeup;
} TaskRunnerState;

/*
 * Structure for query and query plan. These are used to prepare the
 * query and save away the query plan.
 */
typedef struct TaskRunnerQuery {
  /* Fields that need to be defined */
  const char *query;
  size_t nargs;
  int ok;
  Oid argtypes[5]; /* To be able to use array initializers */

  SPIPlanPtr plan;
} TaskRunnerQuery;

extern PGDLLEXPORT Datum tasks_start(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum tasks_stop(PG_FUNCTION_ARGS);

extern PGDLLEXPORT pg_noreturn void TaskRunnerMain(Datum main_arg);
extern PGDLLEXPORT void TaskRunnerExecuteQuery(TaskRunnerQuery *trq,
                                               Datum values[], char nulls[],
                                               bool read_only, int tcount);
