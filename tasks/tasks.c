/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 */

#include "tasks.h"

#include <postgres.h>
#include <fmgr.h>

#include <miscadmin.h>
#include <pgstat.h>
#include <signal.h>

#include "catalog/pg_type_d.h"
#include "postgres_ext.h"
#include "utils/palloc.h"
#include "utils/resowner.h"

#include <access/xact.h>
#include <catalog/objectaccess.h>
#include <catalog/pg_proc.h>
#include <catalog/pg_type.h>
#include <executor/spi.h>
#include <parser/parse_func.h>
#include <postmaster/bgworker.h>
#include <postmaster/interrupt.h>
#include <storage/dsm.h>
#include <storage/dsm_impl.h>
#include <storage/ipc.h>
#include <storage/latch.h>
#include <storage/shm_toc.h>
#include <tcop/tcopprot.h>
#include <utils/acl.h>
#include <utils/backend_status.h>
#include <utils/inval.h>
#include <utils/lsyscache.h>
#include <utils/memutils.h>
#include <utils/regproc.h>
#include <utils/snapmgr.h>
#include <utils/timestamp.h>

PG_MODULE_MAGIC;

/*
 * Callbacks for PostgreSQL.
 */
#if 0
static void TaskRunnerShmExit(int code, Datum arg);
#endif
static void TaskRunnerRelCallback(Datum arg, Oid relid);

/*
 * Processing functions for the task runner.
 */
static void TaskRunnerShutdown(void);
static void TaskRunnerReloadConfig(void);
static void TaskRunnerUpdateState(TaskRunnerState *state);
static void TaskRunnerAttachSession(dsm_handle handle);
static void TaskRunnerExecuteNext(TaskRunnerState *state);
static void TaskRunnerPrepareAndExecute(TaskRunnerQuery *trq, Datum values[],
                                        char nulls[], bool read_only,
                                        int tcount);

static Oid TaskTableOid = InvalidOid;
static bool TaskTableChanged = false;
static int TaskTotalRunners = 4;
static char *TaskRunnerDatabase = NULL;

static TaskRunnerQuery getnextwakeup = {
    .query = "select min(task_sched) from task",
    .ok = SPI_OK_SELECT,
    .nargs = 0,
};

static TaskRunnerQuery getnexttask = {
    .query =
        "select * from task where task_sched <= now() order by task_sched desc "
        "for no key update skip locked",
    .ok = SPI_OK_SELECT,
    .nargs = 0,
};

static TaskRunnerQuery deletetask = {
    .query = "delete from task where task_id = $1",
    .ok = SPI_OK_DELETE,
    .nargs = 1,
    .argtypes = {INT4OID},
};

MemoryContext TaskRunnerScratchMemoryContext;

/*
 * Main entrypoint for task runner.
 *
 * Task runners read tasks from the task table and execute them. We avoid
 * keeping an internal list of tasks to execute since the task table can
 * change and then we need to synchronize the queue with the task table.
 *
 * Each task runner deals with scheduling itself and does not require a
 * centralized scheduler.
 */
void TaskRunnerMain(Datum main_arg) {
  int rc, wake_events;
  TaskRunnerState state = {.next_wakeup = GetCurrentTimestamp()};
  ResourceOwner resowner;

  pqsignal(SIGHUP, SignalHandlerForConfigReload);
  pqsignal(SIGINT, SignalHandlerForShutdownRequest);
  pqsignal(SIGTERM, die);
  BackgroundWorkerUnblockSignals();

  if (IsBinaryUpgrade)
    proc_exit(0);

  /* Set up resource owner and memory context to work in */
  Assert(CurrentResourceOwner == NULL);
  resowner = ResourceOwnerCreate(NULL, "TaskRunnerMain");
  CurrentResourceOwner = resowner;
  CurrentMemoryContext = AllocSetContextCreate(
      TopMemoryContext, "TaskRunner", ALLOCSET_DEFAULT_SIZES);
  TaskRunnerScratchMemoryContext = AllocSetContextCreate(
      CurrentMemoryContext, "TaskRunnerScratch", ALLOCSET_DEFAULT_SIZES);

  TaskRunnerAttachSession(DatumGetUInt32(main_arg));

  /*
   * This clears the resource owner, so we re-set it after.
   */
  BackgroundWorkerInitializeConnection(TaskRunnerDatabase, NULL, 0);
  CurrentResourceOwner = resowner;

  elog(LOG,
       "%s:%05d: init CurrentResourceOwner=%p, CurrentMemoryContext=%p, "
       "TopMemoryContext=%p, TaskRunnerDatabase=%s",
       __func__,
       __LINE__,
       CurrentResourceOwner,
       CurrentMemoryContext,
       TopMemoryContext,
       TaskRunnerDatabase);

  for (;;) {
    long timeout = 0;

    CHECK_FOR_INTERRUPTS();

    if (ShutdownRequestPending)
      TaskRunnerShutdown();

    if (ConfigReloadPending)
      TaskRunnerReloadConfig();

    AcceptInvalidationMessages();

    if (TaskTableChanged)
      TaskRunnerUpdateState(&state);

    /*
     * If next wakeup is in the past, we fetch and execute the next
     * task.
     */
    if (state.next_wakeup < GetCurrentTimestamp())
      TaskRunnerExecuteNext(&state);

    /*
     * Look for the next wakeup time.
     */
    TaskRunnerUpdateState(&state);

    /* Wait for more work. */
    wake_events = WL_LATCH_SET | WL_EXIT_ON_PM_DEATH;

    /*
     * WaitEventSetWait can handle negative timouts so no need to check
     * that timeout is not negative.
     *
     * WaitEventSetWait will be passed -1 as timeout if WL_TIMEOUT is
     * not set, so no need to deal with "timeout" if WL_TIMEOUT is not
     * set.
     */
    if (state.use_timeout) {
      wake_events |= WL_TIMEOUT;
      /* Timestamp is in microseconds, timeout is in milliseconds. */
      timeout = (state.next_wakeup - GetCurrentTimestamp()) / 1000;
    }

    rc = WaitLatch(MyLatch, wake_events, timeout, 0);

    if (rc & WL_LATCH_SET)
      ResetLatch(MyLatch);
  }

  /* Restart runner if we get here. Shutdown requests are handled above. */
  proc_exit(1);
}

static void TaskRunnerShutdown(void) {
  ereport(LOG, (errmsg("task runner terminating due to a shutdown request")));
  proc_exit(0);
}

static void TaskRunnerRelCallback(Datum arg, Oid relid) {
  if (!OidIsValid(TaskTableOid)) {
    Oid nspoid = get_namespace_oid("tasks", false);
    TaskTableOid = get_relname_relid("task", nspoid);
  }

  /* TaskTableOid can be InvalidOid in case the table does not exist,
   * but that is OK since it just means that the task table is not
   * defined yet, hence has not changed. */

  if (OidIsValid(TaskTableOid) && TaskTableOid == relid)
    TaskTableChanged = true;
  elog(LOG,
       "TaskTableOid=%d, TaskTableChanged=%s",
       TaskTableOid,
       TaskTableChanged ? "yes" : "no");
}

static void TaskRunnerReloadConfig(void) {
  ConfigReloadPending = false;
  ProcessConfigFile(PGC_SIGHUP);
}

/* Update execution state to contain information to schedule next wakeup
 * time. Note that the next wakeup time can be in the past.
 */
static void TaskRunnerUpdateState(TaskRunnerState *state) {
  MemoryContext old_context;

  old_context = MemoryContextSwitchTo(TaskRunnerScratchMemoryContext);

  SetCurrentStatementStartTimestamp();
  AbortOutOfAnyTransaction();
  StartTransactionCommand();

  if (SPI_connect() != SPI_OK_CONNECT)
    elog(ERROR, "SPI_connect failed");

  PushActiveSnapshot(GetTransactionSnapshot());

  TaskRunnerPrepareAndExecute(&getnextwakeup, NULL, NULL, true, 1);

  /*
   * We either have one or zero rows. If we have zero rows, we can idle until
   * somebody adds something to the table, so we can skip waiting for a
   * timeout.  If we have one row, have have the next wakeup time in the
   * result.
   */
  state->use_timeout = (SPI_processed > 0);
  if (SPI_processed > 0) {
    bool isnull;
    HeapTuple tup = SPI_tuptable->vals[0];
    Datum value = SPI_getbinval(tup, SPI_tuptable->tupdesc, 1, &isnull);

    if (isnull)
      elog(ERROR, "null scheduled time");

    state->next_wakeup = DatumGetTimestampTz(value);
  }

  if (SPI_finish() != SPI_OK_FINISH)
    elog(ERROR, "%s: SPI_finish() failed", __func__);

  PopActiveSnapshot();
  CommitTransactionCommand();

  elog(LOG,
       "%s: use_timeout=%s, next wakeup at %s",
       __func__,
       state->use_timeout ? "yes" : "no",
       timestamptz_to_str(state->next_wakeup));
  MemoryContextSwitchTo(old_context);
  MemoryContextReset(TaskRunnerScratchMemoryContext);
}

#if 0
static void TaskRunnerShmExit(int code, Datum arg) {
  dsm_detach((dsm_segment *)DatumGetPointer(arg));
}

static void TaskRunnerAttachSession(dsm_handle handle) {
	dsm_segment *seg;
  shm_toc *toc;

  seg = dsm_attach(handle);
  if (seg == NULL)
    ereport(ERROR,
            (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
             errmsg("could not map dynamic shared memory segment")));

  toc = shm_toc_attach(TASK_RUNNER_MAGIC, dsm_segment_address(seg));
  if (toc == NULL)
    ereport(ERROR,
            (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
             errmsg("invalid magic number in dynamic shared memory segment")));

  before_shmem_exit(TaskRunnerShmExit, PointerGetDatum(seg));
}
#else
static void TaskRunnerAttachSession(dsm_handle handle) {}
#endif

static void TaskRunnerExecuteNext(TaskRunnerState *state) {
  MemoryContext old_context;
  bool owner_isnull, exec_isnull;
  int task_id_attno, task_owner_attno, task_exec_attno;
  int32 task_id;
  Oid task_owner;
  HeapTuple tup;
  TupleDesc tupdesc;
  Name task_exec;

  old_context = MemoryContextSwitchTo(TaskRunnerScratchMemoryContext);

  SetCurrentStatementStartTimestamp();
  AbortOutOfAnyTransaction();
  StartTransactionCommand();

  if (SPI_connect_ext(SPI_OPT_NONATOMIC) != SPI_OK_CONNECT)
    elog(ERROR, "%s: SPI_connect_ext failed", __func__);

  PushActiveSnapshot(GetTransactionSnapshot());

  TaskRunnerPrepareAndExecute(&getnexttask, NULL, NULL, false, 1);

  /*
   * We either have one or zero rows. If we have zero rows, tasks that are
   * ready to run have been picked up by other runners, so we just exit and
   * let the caller figure out when to wake up again.
   */
  Assert(SPI_processed <= 1);
  if (SPI_processed == 0)
    return;

  tup = SPI_tuptable->vals[0];
  tupdesc = SPI_tuptable->tupdesc;
  task_id_attno = SPI_fnumber(tupdesc, "task_id");
  task_owner_attno = SPI_fnumber(tupdesc, "task_owner");
  task_exec_attno = SPI_fnumber(tupdesc, "task_exec");

  task_owner = DatumGetObjectId(
      SPI_getbinval(tup, tupdesc, task_owner_attno, &owner_isnull));
  if (!has_privs_of_role(GetUserId(), task_owner))
    ereport(ERROR,
            (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
             errmsg("permission denied to execute task")));
  task_exec =
      DatumGetName(SPI_getbinval(tup, tupdesc, task_exec_attno, &exec_isnull));

  if (!exec_isnull) {
    LOCAL_FCINFO(fcinfo, 2);
    Oid argtypes[] = {TIMESTAMPTZOID, JSONBOID};
    AclResult aclresult;
    PgStat_FunctionCallUsage fcusage;
    FmgrInfo finfo;
    bool sched_isnull, config_isnull, task_id_isnull;
    List *namelist;
    Oid proc_oid;

    int config_attno = SPI_fnumber(tupdesc, "task_config");
    int sched_attno = SPI_fnumber(tupdesc, "task_sched");

    /* Delete the task before executing */
    task_id = DatumGetObjectId(
        SPI_getbinval(tup, tupdesc, task_id_attno, &task_id_isnull));
    elog(LOG, "deleting task with task id %d", task_id);
    TaskRunnerPrepareAndExecute(&deletetask,
                                (Datum[]){Int32GetDatum(task_id)},
                                (char[]){' '},
                                false,
                                0);

    namelist = stringToQualifiedNameList(NameStr(*task_exec), NULL);
    proc_oid = LookupFuncName(namelist, 2, argtypes, false);
    aclresult = object_aclcheck(
        ProcedureRelationId, proc_oid, GetUserId(), ACL_EXECUTE);
    if (aclresult != ACLCHECK_OK)
      aclcheck_error(aclresult, OBJECT_FUNCTION, NameStr(*task_exec));

    InvokeFunctionExecuteHook(proc_oid);
    fmgr_info(proc_oid, &finfo);
    InitFunctionCallInfoData(*fcinfo, &finfo, 0, InvalidOid, NULL, NULL);

    fcinfo->args[0].value =
        SPI_getbinval(tup, tupdesc, sched_attno, &sched_isnull);
    fcinfo->args[0].isnull = sched_isnull;
    fcinfo->args[1].value =
        SPI_getbinval(tup, tupdesc, config_attno, &config_isnull);
    fcinfo->args[1].isnull = config_isnull;

    elog(LOG, "%s: found %s, so executing", __func__, NameStr(*task_exec));

    pgstat_init_function_usage(fcinfo, &fcusage);
    pgstat_report_activity(STATE_RUNNING, NULL);
    FunctionCallInvoke(fcinfo);
    pgstat_report_activity(STATE_IDLE, NULL);
    pgstat_end_function_usage(&fcusage, true);

    elog(LOG, "%s: %s executed", __func__, NameStr(*task_exec));
  }

  if (SPI_finish() != SPI_OK_FINISH)
    elog(ERROR, "%s: SPI_finish() failed", __func__);

  PopActiveSnapshot();
  CommitTransactionCommand();

  debug_query_string = NULL;
  pgstat_report_activity(STATE_RUNNING, debug_query_string);

  MemoryContextSwitchTo(old_context);
  MemoryContextReset(TaskRunnerScratchMemoryContext);
}

void _PG_init(void) {
  BackgroundWorker worker;

  if (!process_shared_preload_libraries_in_progress)
    return;

  CacheRegisterRelcacheCallback(TaskRunnerRelCallback, (Datum)0);
  DefineCustomIntVariable("tasks.workers",
                          "Number of workers.",
                          NULL,
                          &TaskTotalRunners,
                          4,
                          1,
                          100,
                          PGC_POSTMASTER,
                          0,
                          NULL,
                          NULL,
                          NULL);

  DefineCustomStringVariable("tasks.database",
                             "Database to connect to.",
                             NULL,
                             &TaskRunnerDatabase,
                             "postgres",
                             PGC_POSTMASTER,
                             0,
                             NULL,
                             NULL,
                             NULL);

  MarkGUCPrefixReserved("tasks");

  memset(&worker, 0, sizeof(worker));
  worker.bgw_flags =
      BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
  worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
  sprintf(worker.bgw_library_name, "tasks");
  sprintf(worker.bgw_function_name, "TaskRunnerMain");
  worker.bgw_notify_pid = 0;

  /*
   * Restart time is set to 30 seconds since it might be that either
   * the database or the extension is not created yet. In that case,
   * we keep retrying until the database and the extension is created.
   */
  worker.bgw_restart_time = 30;

  for (int i = 1; i <= TaskTotalRunners; i++) {
    snprintf(worker.bgw_name, BGW_MAXLEN, "Task Runner %d", i);
    snprintf(worker.bgw_type, BGW_MAXLEN, "Task Runner");
    worker.bgw_main_arg = Int32GetDatum(i);

    RegisterBackgroundWorker(&worker);
  }
}

void TaskRunnerPrepareAndExecute(TaskRunnerQuery *trq, Datum values[],
                                 char nulls[], bool read_only, int tcount) {
  int rc;

  Assert(plan_var != NULL && query != NULL);

  debug_query_string = trq->query;
  pgstat_report_activity(STATE_RUNNING, debug_query_string);

  if (trq->plan == NULL) {
    SPIPlanPtr plan = SPI_prepare(trq->query, trq->nargs, trq->argtypes);
    if (plan == NULL)
      elog(ERROR, "SPI_prepare failed for \"%s\"", trq->query);
    if (SPI_keepplan(plan))
      elog(ERROR, "SPI_keepplan failed for \"%s\"", trq->query);
    trq->plan = plan;
  }

  rc = SPI_execute_plan(trq->plan, values, nulls, read_only, tcount);
  if (rc != trq->ok)
    elog(ERROR, "SPI_execute_plan failed for \"%s\"", trq->query);

  debug_query_string = NULL;
  pgstat_report_activity(STATE_IDLE, debug_query_string);
}
