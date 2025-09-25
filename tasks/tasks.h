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

#define TASK_RUNNER_MAGIC 0xdeadbeef /* Temporary magic number */

typedef struct TaskRunnerState {
  TimestampTz next_wakeup;
  bool use_timeout;
} TaskRunnerState;

/*
 * Structure for query and query plan.
 */
typedef struct TaskRunnerQuery {
  SPIPlanPtr plan;
  const char *query;
  int ok;
  size_t nargs;
  Oid argtypes[5]; /* To be able to use array initializers */
} TaskRunnerQuery;

extern PGDLLEXPORT void TaskRunnerMain(Datum main_arg) pg_attribute_noreturn();
