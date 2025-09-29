/*
 * Copyright 2025 Mats Kindahl.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "memview.h"

#include <postgres.h>
#include <fmgr.h>

#include <funcapi.h>
#include <miscadmin.h>

#include <commands/trigger.h>
#include <storage/dsm.h>
#include <storage/lwlock.h>
#include <storage/shm_toc.h>
#include <storage/shmem.h>
#include <utils/builtins.h>

PG_MODULE_MAGIC;

#define TRACE(FMT, ...) elog(DEBUG1, "%s: " FMT, __func__, ##__VA_ARGS__)

#define MEMVIEW_MAGIC 0x12345678

enum {
  MEMVIEW_KEY_CONTEXT,
  MEMVIEW_KEY_MANIFEST,
};

PG_FUNCTION_INFO_V1(memview_row_delete);
PG_FUNCTION_INFO_V1(memview_row_insert);
PG_FUNCTION_INFO_V1(memview_row_update);
PG_FUNCTION_INFO_V1(memview_view_scan);
PG_FUNCTION_INFO_V1(memview_view_reset);

/*
 * Structure with the shared memory state containing, among other
 * things, the DSM handle.
 */
typedef struct MemoryViewState {
  LWLock lock;
  dsm_handle handle;
} MemoryViewState;

static MemoryViewState* memview_state = NULL;
static MemoryViewSession memview_session = {.segment = 0};

/*
 * Get a session DSM handle.
 *
 * This will set up the DSM segment if necessary.
 */
dsm_handle memview_dsm_handle(void) {
  MemoryContext old_context;
  Size size;
  dsm_segment* seg;
  shm_toc* toc;
  shm_toc_estimator estimator;
  MemoryViewHeader* header;
  MemoryViewRecord* manifest;
  Size manifest_size;

  if (memview_session.segment != NULL) {
    TRACE("returning existing handle %d",
          dsm_segment_handle(memview_session.segment));
    return dsm_segment_handle(memview_session.segment);
  }

  TRACE("creating DSM segment and handle");

  old_context = MemoryContextSwitchTo(TopMemoryContext);

  shm_toc_initialize_estimator(&estimator);
  manifest_size = mul_size(sizeof(MemoryViewRecord), MEMVIEW_MAX_RECORDS);
  shm_toc_estimate_keys(&estimator, 2);
  shm_toc_estimate_chunk(&estimator, sizeof(int));
  shm_toc_estimate_chunk(&estimator, manifest_size);

  size = shm_toc_estimate(&estimator);
  seg = dsm_create(size, DSM_CREATE_NULL_IF_MAXSEGMENTS);
  if (seg == NULL) {
    MemoryContextSwitchTo(old_context);
    return DSM_HANDLE_INVALID;
  }
  toc = shm_toc_create(MEMVIEW_MAGIC, dsm_segment_address(seg), size);

  /* Add the information about the view */
  header = shm_toc_allocate(toc, sizeof(MemoryViewHeader));
  header->nrecords = 0;
  shm_toc_insert(toc, MEMVIEW_KEY_CONTEXT, header);

  /* Add the manifest, which is an array of fixed-size records */
  manifest = shm_toc_allocate(toc, manifest_size);
  memset(manifest, 0, manifest_size);
  shm_toc_insert(toc, MEMVIEW_KEY_MANIFEST, manifest);

  dsm_pin_segment(seg);
  dsm_pin_mapping(seg);

  memview_session.segment = seg;
  memview_session.header = header;
  memview_session.manifest = manifest;

  MemoryContextSwitchTo(old_context);

  return dsm_segment_handle(seg);
}

/*
 * Initialize the shared memory state.
 */
static bool memview_init_shmem(void) {
  bool found;

  TRACE("initializing shared memory");
  LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
  memview_state = ShmemInitStruct("memview", sizeof(MemoryViewState), &found);
  if (!found) {
    LWLockInitialize(&memview_state->lock, LWLockNewTrancheId());
    memview_state->handle = memview_dsm_handle();
  }
  LWLockRelease(AddinShmemInitLock);
  TRACE("existing shared memory was %sfound", found ? "" : "NOT ");

  LWLockRegisterTranche(memview_state->lock.tranche, "memview");

  return found;
}

MemoryViewSession* memview_session_get(dsm_handle handle) {
  memview_init_shmem();

  /* If memory view segment is not there, attach to it. We need to
   * take the lock before checking the session otherwise somebody else
   * could come along and try to create a segment after we have
   * checked the value. */
  if (memview_session.segment == NULL) {
    shm_toc* toc;
    dsm_segment* seg;
    MemoryViewHeader* header;
    MemoryViewRecord* manifest;

    seg = dsm_attach(handle);
    if (seg == NULL)
      ereport(ERROR,
              (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
               errmsg("could not map dynamic shared memory segment")));

    toc = shm_toc_attach(MEMVIEW_MAGIC, dsm_segment_address(seg));
    header = shm_toc_lookup(toc, MEMVIEW_KEY_CONTEXT, false);
    manifest = shm_toc_lookup(toc, MEMVIEW_KEY_MANIFEST, false);

    memview_session.segment = seg;
    memview_session.header = header;
    memview_session.manifest = manifest;

    dsm_pin_mapping(seg);
  }

  return &memview_session;
}

Datum memview_row_insert(PG_FUNCTION_ARGS) {
  pid_t pid = PG_GETARG_INT32(0);
  Oid owner = PG_GETARG_OID(1);
  Name descr = PG_GETARG_NAME(2);
  MemoryViewRecord* record;
  MemoryViewSession* session;

  memview_init_shmem();

  TRACE("inserting row (%d,%d,'%s'::regrole,%s)",
        MyDatabaseId,
        pid,
        NameStr(*DatumGetName(
            DirectFunctionCall1(pg_get_userbyid, ObjectIdGetDatum(owner)))),
        NameStr(*descr));

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  record = &session->manifest[session->header->nrecords++];
  record->dboid = MyDatabaseId;
  record->pid = pid;
  record->owner = owner;
  namestrcpy(&record->description, NameStr(*descr));
  LWLockRelease(&memview_state->lock);
  PG_RETURN_VOID();
}

Datum memview_view_reset(PG_FUNCTION_ARGS) {
  MemoryViewSession* session;

  memview_init_shmem();

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  session->header->nrecords = 0;
  LWLockRelease(&memview_state->lock);
  PG_RETURN_VOID();
}

Datum memview_row_delete(PG_FUNCTION_ARGS) {
  int32 record_no = PG_GETARG_INT32(0);
  MemoryViewSession* session;

  if (CALLED_AS_TRIGGER(fcinfo)) {
    TriggerData* trigdata = (TriggerData*)fcinfo->context;

    if (!TRIGGER_FIRED_INSTEAD(trigdata->tg_event))
      ereport(ERROR,
              (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
               errmsg("must be called as trigger or directly")));
  }

  memview_init_shmem();

  TRACE("deleting row %d", record_no);

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  memmove(&session->manifest[record_no],
          &session->manifest[record_no + 1],
          (session->header->nrecords - record_no) * sizeof(MemoryViewRecord));
  --session->header->nrecords;
  LWLockRelease(&memview_state->lock);
  PG_RETURN_VOID();
}

Datum memview_row_update(PG_FUNCTION_ARGS) {
  int32 record_no = PG_GETARG_INT32(0);
  pid_t pid = PG_GETARG_INT32(1);
  Oid owner = PG_GETARG_OID(2);
  Name descr = PG_GETARG_NAME(3);
  MemoryViewRecord* record;
  MemoryViewSession* session;

  memview_init_shmem();

  TRACE("updating row %d with (%d,'%s'::regrole,%s)",
        record_no,
        pid,
        NameStr(*DatumGetName(
            DirectFunctionCall1(pg_get_userbyid, ObjectIdGetDatum(owner)))),
        NameStr(*descr));

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  record = &session->manifest[record_no];
  /* No need to change the database OID. It remains the same */
  record->pid = pid;
  record->owner = owner;
  namestrcpy(&record->description, NameStr(*descr));
  LWLockRelease(&memview_state->lock);
  PG_RETURN_VOID();
}

/*
 * Get the the memory view as a result set.
 *
 * Note that we lock the region for the entire result set. It is
 * possible to optimize the locking here but that requires a little
 * more care, so we don't do that right now.
 */
Datum memview_view_scan(PG_FUNCTION_ARGS) {
  FuncCallContext* funcctx;

  memview_init_shmem();

  if (SRF_IS_FIRSTCALL()) {
    MemoryContext oldcontext;
    TupleDesc tupdesc;
    MemoryViewSession* session;

    funcctx = SRF_FIRSTCALL_INIT();

    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
      ereport(ERROR,
              (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
               errmsg("function returning record called in context "
                      "that cannot accept type record")));

    LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
    session = memview_session_get(memview_state->handle);
    funcctx->max_calls = session->header->nrecords;
    funcctx->user_fctx = session;
    LWLockRelease(&memview_state->lock);

    funcctx->tuple_desc = BlessTupleDesc(tupdesc);

    oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
    MemoryContextSwitchTo(oldcontext);
  }

  CHECK_FOR_INTERRUPTS();

  funcctx = SRF_PERCALL_SETUP();

  if (funcctx->call_cntr < funcctx->max_calls) {
    MemoryViewSession* session = funcctx->user_fctx;
    bool nulls[5] = {0};
    Datum values[5] = {0};
    HeapTuple tuple;
    MemoryViewRecord* record;

    LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);

    record = &session->manifest[funcctx->call_cntr];

    values[0] = UInt32GetDatum(funcctx->call_cntr);
    values[1] = ObjectIdGetDatum(record->dboid);
    if (record->owner) {
      values[2] = Int32GetDatum(record->pid);
      values[3] = ObjectIdGetDatum(record->owner);
      values[4] = NameGetDatum(&record->description);
    } else {
      nulls[2] = nulls[3] = nulls[4] = true;
    }

    tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

    LWLockRelease(&memview_state->lock);

    SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
  }

  SRF_RETURN_DONE(funcctx);
}
