/*
 * This file and its contents are licensed under the Apache License 2.0.
 * Please see the included NOTICE for copyright information and
 * LICENSE-APACHE for a copy of the license.
 */

#include "tasks.h"

#include <postgres.h>
#include <fmgr.h>

#include <c.h>
#include <limits.h>
#include <miscadmin.h>
#include <pgstat.h>
#include <postgres_ext.h>
#include <signal.h>

#include <access/xact.h>
#include <catalog/objectaccess.h>
#include <catalog/pg_proc.h>
#include <catalog/pg_type.h>
#include <executor/spi.h>
#include <nodes/pg_list.h>
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
#include <utils/palloc.h>
#include <utils/regproc.h>
#include <utils/resowner.h>
#include <utils/snapmgr.h>
#include <utils/timestamp.h>
#include <utils/varlena.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(tasks_start);

/*
 * Callbacks for PostgreSQL.
 */
#if 0
static void TaskRunnerShmExit(int code, Datum arg);
static void TaskRunnerRelCallback(Datum arg, Oid relid);
#endif

/*
 * Processing functions for the task runner.
 */
static void TaskRunnerShutdown(void);
static void TaskRunnerReloadConfig(void);
static void TaskRunnerUpdateState(TaskRunnerState *state);
static void TaskRunnerAttachSession(dsm_handle handle);
static void TaskRunnerExecuteNext(TaskRunnerState *state);

#if 0
static Oid TaskTableOid = InvalidOid;
#endif
static bool TaskTableChanged = false;
static int TaskTotalRunners = 4;
static int TaskRunnerRestartTime = 30;
static int TaskRunnerNapTime = 1;
static char *TaskRunnerDatabases = NULL;

static TaskRunnerQuery getnextwakeup = {
    /* Do we need to skip locked rows in some way? */
    .query =
        "select coalesce(min(task_sched), clock_timestamp() + $1 * '1 "
        "second'::interval) from tasks.task",
    .ok = SPI_OK_SELECT,
    .nargs = 1,
    .argtypes = {INT2OID},
};

static TaskRunnerQuery getnexttask = {
    .query =
        "select * from tasks.task where task_sched <= now() order by "
        "task_sched desc for no key update skip locked",
    .ok = SPI_OK_SELECT,
    .nargs = 0,
};

static TaskRunnerQuery deletetask = {
    .query = "delete from tasks.task where task_id = $1",
    .ok = SPI_OK_DELETE,
    .nargs = 1,
    .argtypes = {INT4OID},
};

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
  TaskRunnerState state = {.next_wakeup = GetCurrentTimestamp()};
  ResourceOwner resowner;
  TaskRunnerArgs args;

  pqsignal(SIGHUP, SignalHandlerForConfigReload);
  pqsignal(SIGTERM, SignalHandlerForShutdownRequest);
  BackgroundWorkerUnblockSignals();

  if (IsBinaryUpgrade)
    proc_exit(0);

  memcpy(&args, MyBgworkerEntry->bgw_extra, sizeof(args));

  /* Set up resource owner and memory context to work in */
  Assert(CurrentResourceOwner == NULL);
  resowner = ResourceOwnerCreate(NULL, "TaskRunnerMain");
  CurrentResourceOwner = resowner;
  CurrentMemoryContext = AllocSetContextCreate(
      TopMemoryContext, "TaskRunner", ALLOCSET_DEFAULT_SIZES);
  TaskRunnerAttachSession(DatumGetUInt32(main_arg));

  /*
   * Initializing the connection clears the resource owner, so we
   * re-set it after. Role OID and database OID is not valid if we are
   * called from _PG_init. Then we use the database name instead.
   */
  if (OidIsValid(args.roleoid))
    BackgroundWorkerInitializeConnectionByOid(args.dboid, args.roleoid, 0);
  else
    BackgroundWorkerInitializeConnection(args.dbname, NULL, 0);

  CurrentResourceOwner = resowner;

  /*
   * Make the application name visible in pg_stat_activity.
   */
  pgstat_report_appname(MyBgworkerEntry->bgw_name);

  for (;;) {
    long timeout = 0;

    if (ShutdownRequestPending)
      TaskRunnerShutdown();

    CHECK_FOR_INTERRUPTS();

    if (ConfigReloadPending)
      TaskRunnerReloadConfig();

#if 0
    AcceptInvalidationMessages();
#endif

    SetCurrentStatementStartTimestamp();
    AbortOutOfAnyTransaction();
    StartTransactionCommand();

    if (SPI_connect_ext(SPI_OPT_NONATOMIC) != SPI_OK_CONNECT)
      elog(ERROR, "%s: SPI_connect_ext failed", __func__);

    PushActiveSnapshot(GetTransactionSnapshot());

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

    if (SPI_finish() != SPI_OK_FINISH)
      elog(ERROR, "%s: SPI_finish() failed", __func__);

    PopActiveSnapshot();
    CommitTransactionCommand();
    pgstat_report_stat(true);

    /* Timestamp is in microseconds, timeout is in milliseconds. */
    timeout = (state.next_wakeup - GetCurrentTimestamp()) / 1000;

    if (timeout > 0) {
      int rc = WaitLatch(
          MyLatch, WL_LATCH_SET | WL_EXIT_ON_PM_DEATH | WL_TIMEOUT, timeout, 0);

      if (rc & WL_LATCH_SET)
        ResetLatch(MyLatch);
    }
  }

  /* Restart runner if we get here. Shutdown requests are handled above. */
  proc_exit(1);
}

static void TaskRunnerShutdown(void) {
  ereport(LOG, (errmsg("task runner terminating due to a shutdown request")));
  proc_exit(0);
}

#if 0
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
  elog(LOG, "%s: TaskTableOid=%d, TaskTableChanged=%s", __func__, TaskTableOid,
       TaskTableChanged ? "yes" : "no");
}
#endif

static void TaskRunnerReloadConfig(void) {
  ConfigReloadPending = false;
  ProcessConfigFile(PGC_SIGHUP);
}

/* Update execution state to contain information to schedule next wakeup
 * time. Note that the next wakeup time can be in the past.
 */
static void TaskRunnerUpdateState(TaskRunnerState *state) {
  bool isnull;
  HeapTuple tup;
  Datum value;

  TaskRunnerExecuteQuery(&getnextwakeup,
                         (Datum[]){Int16GetDatum(TaskRunnerNapTime)},
                         (char[]){' '},
                         true,
                         1);

  tup = SPI_tuptable->vals[0];
  value = SPI_getbinval(tup, SPI_tuptable->tupdesc, 1, &isnull);

  if (isnull)
    elog(LOG, "next start time was null");

  state->next_wakeup = DatumGetTimestampTz(value);
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
  bool owner_isnull, exec_isnull;
  int task_id_attno, task_owner_attno, task_exec_attno;
  int32 task_id;
  Oid task_owner;
  HeapTuple tup;
  TupleDesc tupdesc;
  Name task_exec;

  TaskRunnerExecuteQuery(&getnexttask, NULL, NULL, false, 1);

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
    char *activity;
    AclResult aclresult;
    PgStat_FunctionCallUsage fcusage;
    FmgrInfo finfo;
    bool sched_isnull, config_isnull, task_id_isnull;
    List *namelist;
    Oid proc_oid;

    int config_attno = SPI_fnumber(tupdesc, "task_config");
    int sched_attno = SPI_fnumber(tupdesc, "task_sched");

    /* Delete the task before executing. This will be part of the
     * transaction that reads and locks the row task row, so will not
     * be committed until we've executed. */
    task_id = DatumGetObjectId(
        SPI_getbinval(tup, tupdesc, task_id_attno, &task_id_isnull));

    TaskRunnerExecuteQuery(&deletetask,
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

    activity = psprintf("executing %s", NameStr(*task_exec));

    pgstat_report_activity(STATE_RUNNING, activity);

    pgstat_init_function_usage(fcinfo, &fcusage);
    FunctionCallInvoke(fcinfo);
    pgstat_end_function_usage(&fcusage, true);

    pgstat_report_activity(STATE_IDLE, NULL);
  }
}

/*
 * Prepare and execute a query.
 *
 * This prepares the query defined in the TaskRunnerQuery structure
 * and saves away the plan after it is prepared.
 */
void TaskRunnerExecuteQuery(TaskRunnerQuery *query, Datum values[],
                            char nulls[], bool read_only, int tcount) {
  int rc;

  Assert(plan_var != NULL && query != NULL);

  debug_query_string = query->query;
  pgstat_report_activity(STATE_RUNNING, query->query);

  if (query->plan == NULL) {
    SPIPlanPtr plan = SPI_prepare(query->query, query->nargs, query->argtypes);
    if (plan == NULL)
      elog(ERROR, "SPI_prepare failed for \"%s\"", query->query);
    if (SPI_keepplan(plan))
      elog(ERROR, "SPI_keepplan failed for \"%s\"", query->query);
    query->plan = plan;
  }

  rc = SPI_execute_plan(query->plan, values, nulls, read_only, tcount);
  if (rc != query->ok)
    elog(ERROR, "SPI_execute_plan failed for \"%s\"", query->query);

  debug_query_string = NULL;
  pgstat_report_activity(STATE_IDLE, NULL);
}

Datum tasks_start(PG_FUNCTION_ARGS) {
  BackgroundWorker worker;
  TaskRunnerArgs args = {
      .roleoid = GetUserId(),
      .dboid = MyDatabaseId,
  };

  memset(&worker, 0, sizeof(worker));
  worker.bgw_flags =
      BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
  worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
  worker.bgw_restart_time = TaskRunnerRestartTime;
  sprintf(worker.bgw_library_name, "tasks");
  sprintf(worker.bgw_function_name, "TaskRunnerMain");
  snprintf(worker.bgw_type, BGW_MAXLEN, "Task Runner");
  worker.bgw_notify_pid = MyProcPid;
  worker.bgw_main_arg = 0; /* This should be the DSM segment handle */
  memcpy(worker.bgw_extra, &args, sizeof(args));

  for (int i = 1; i <= TaskTotalRunners; i++) {
    BackgroundWorkerHandle *handle;
    BgwHandleStatus status;
    pid_t pid;

    snprintf(worker.bgw_name, BGW_MAXLEN, "%s %d", worker.bgw_type, i);

    if (!RegisterDynamicBackgroundWorker(&worker, &handle))
      ereport(ERROR,
              (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
               errmsg("could not register background process"),
               errhint("You may need to increase \"max_worker_processes\".")));
    status = WaitForBackgroundWorkerStartup(handle, &pid);
    if (status != BGWH_STARTED)
      ereport(ERROR,
              (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
               errmsg("could not start background process"),
               errhint("More details may be available in the server log.")));
  }

  PG_RETURN_VOID();
}

#if 0
static bool check_databases(char **newval, void **extra, GucSource source) {
  char *rawstring = pstrdup(*newval);
  List *dblist;

  if (!SplitIdentifierString(rawstring, ',', &dblist)) {
    GUC_check_errdetail("List syntax is invalid.");
    pfree(rawstring);
    list_free(dblist);
    return false;
  }
}
#endif

void _PG_init(void) {
  BackgroundWorker worker;
  char *rawstring;
  List *dblist;
  ListCell *lc;

  TaskRunnerArgs args = {
      .dboid = InvalidOid,
      .roleoid = InvalidOid,
  };

  if (!process_shared_preload_libraries_in_progress)
    return;

#if 0
  CacheRegisterRelcacheCallback(TaskRunnerRelCallback, (Datum)0);
#endif

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

  DefineCustomIntVariable("tasks.restart_time",
                          "Restart time for workers, in seconds.",
                          NULL,
                          &TaskRunnerRestartTime,
                          30,
                          1,
                          INT_MAX,
                          PGC_POSTMASTER,
                          GUC_UNIT_S,
                          NULL,
                          NULL,
                          NULL);

  DefineCustomIntVariable("tasks.nap_time",
                          "Nap time for workers, in seconds.",
                          NULL,
                          &TaskRunnerNapTime,
                          1,
                          1,
                          3600,
                          PGC_POSTMASTER,
                          GUC_UNIT_S,
                          NULL,
                          NULL,
                          NULL);

  DefineCustomStringVariable(
      "tasks.databases",
      "Databases to start workers for.",
      "A list of databases to start workers for. Note that if the databases "
      "do not exist, the system will repeatadly try to connect to the "
      "database "
      "until it is created and the extension installed in the database.",
      &TaskRunnerDatabases,
      "postgres",
      PGC_POSTMASTER,
      GUC_LIST_INPUT, /* GUC_LIST_QUOTE cannot be used in extensions, so using
                         GUC_LIST_INPUT */
      NULL,
      NULL,
      NULL);

  MarkGUCPrefixReserved("tasks");

  memset(&worker, 0, sizeof(worker));
  worker.bgw_flags =
      BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
  worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
  worker.bgw_restart_time = TaskRunnerRestartTime;
  snprintf(worker.bgw_library_name, MAXPGPATH, "tasks");
  snprintf(worker.bgw_function_name, BGW_MAXLEN, "TaskRunnerMain");
  snprintf(worker.bgw_type, BGW_MAXLEN, "Task Runner");
  worker.bgw_notify_pid = 0;
  worker.bgw_main_arg = 0; /* This should be the DSM segment handle */

  rawstring = pstrdup(TaskRunnerDatabases);
  if (!SplitIdentifierString(rawstring, ',', &dblist)) {
    pfree(rawstring);
    list_free(dblist);
  }

  foreach (lc, dblist) {
    char *dbname = lfirst(lc);

    for (int i = 1; i <= TaskTotalRunners; i++) {
      snprintf(worker.bgw_name, BGW_MAXLEN, "%s %d", worker.bgw_type, i);
      strncpy(args.dbname, dbname, sizeof(args.dbname));
      memcpy(worker.bgw_extra, &args, sizeof(args));
      RegisterBackgroundWorker(&worker);
    }
  }
}
