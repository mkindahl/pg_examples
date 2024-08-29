
#include <postgres.h>
#include <fmgr.h>

#include <miscadmin.h>
#include <pgstat.h>

#include <access/relscan.h>
#include <access/table.h>
#include <access/tableam.h>
#include <executor/spi.h>
#include <executor/tuptable.h>
#include <postmaster/bgworker.h>
#include <postmaster/interrupt.h>
#include <storage/ipc.h>
#include <storage/latch.h>
#include <tcop/utility.h>
#include <utils/lsyscache.h>
#include <utils/snapmgr.h>

PG_MODULE_MAGIC;

static char *DatabaseName;
static int ReportInterval = 60;

void _PG_init(void);
void WorkerMain(Datum) pg_attribute_noreturn();

static void WorkerInitialize(void) {
  elog(LOG, "%s initialized", MyBgworkerEntry->bgw_name);
}

void WorkerMain(Datum arg) {
  pqsignal(SIGHUP, SignalHandlerForConfigReload);
  pqsignal(SIGTERM, die);

  if (DatabaseName == NULL) {
    elog(NOTICE, "no database to report!");
    proc_exit(0); /* No need to restart */
  }

  BackgroundWorkerUnblockSignals();
  BackgroundWorkerInitializeConnection(DatabaseName, NULL, 0);

  pgstat_report_activity(STATE_RUNNING, "initializing");

  WorkerInitialize();

  pgstat_report_activity(STATE_RUNNING, "reporting");

  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();
  SPI_connect();
  PushActiveSnapshot(GetTransactionSnapshot());

  /* We access the database here to be able to report something. */
  {
    int ret = SPI_execute(
        "SELECT json_build_object('tables', count(*)) FROM pg_class WHERE "
        "reltype != 0",
        true,
        0);

    if (ret != SPI_OK_SELECT)
      elog(FATAL, "SPI_execute failed: error code %d", ret);
  }

  if (SPI_processed != 1)
    elog(FATAL, "not a singleton result");

  elog(LOG,
       "report for database %s: %s",
       DatabaseName,
       SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1));

  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
  pgstat_report_activity(STATE_IDLE, NULL);

  /* Use exit status 1 so the background worker is restarted. */
  proc_exit(1);
}

static void StartBackgroundWorkers(void) {
  BackgroundWorker worker = {
      .bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION,
      .bgw_start_time = BgWorkerStart_RecoveryFinished,
      .bgw_restart_time = ReportInterval,
      .bgw_library_name = "reporter",
      .bgw_function_name = "WorkerMain",
      .bgw_type = "Reporter",
      .bgw_notify_pid = 0,
  };

  snprintf(
      worker.bgw_name, BGW_MAXLEN, "Reporter for database %s", DatabaseName);

  RegisterBackgroundWorker(&worker);
}

void _PG_init(void) {
  if (!process_shared_preload_libraries_in_progress)
    return;

  DefineCustomStringVariable("reporter.database",
                             "Database name.",
                             "Database name to connect to.",
                             &DatabaseName,
                             NULL,
                             PGC_POSTMASTER,
                             0,
                             NULL,
                             NULL,
                             NULL);
  DefineCustomIntVariable("reporter.report_interval",
                          "Report interval.",
                          "Interval in seconds for running reporter.",
                          &ReportInterval,
                          60,
                          60,
                          3600 * 24, /* A day */
                          PGC_POSTMASTER,
                          0,
                          NULL,
                          NULL,
                          NULL);

  StartBackgroundWorkers();
}
