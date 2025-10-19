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
#include <executor/spi.h>
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
PG_FUNCTION_INFO_V1(memview_insert_row_tgfunc);
PG_FUNCTION_INFO_V1(memview_delete_row_tgfunc);
PG_FUNCTION_INFO_V1(memview_update_row_tgfunc);

/*
 * Helper function to get a role name using a role OID.
 *
 * Mostly indended for debugging printouts.
 */
static char* get_role_name(Oid roleoid) {
  return NameStr(*DatumGetName(
      DirectFunctionCall1(pg_get_userbyid, ObjectIdGetDatum(roleoid))));
}

static void memview_row_delete_internal(int32 row_id);
static void memview_row_update_internal(int32 row_id, Oid owner, Name descr);
static void memview_row_insert_internal(Oid owner, Name descr);

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

  /*
   * Estimate the size of the TOC for the DSM segment. It contains a
   * few different fields.
   */
  shm_toc_initialize_estimator(&estimator);
  manifest_size = mul_size(sizeof(MemoryViewRecord), MEMVIEW_MAX_RECORDS);
  shm_toc_estimate_keys(&estimator, 2);

  /* Counter for number of records */
  shm_toc_estimate_chunk(&estimator, sizeof(int));

  /* Memory for the actual manifest */
  shm_toc_estimate_chunk(&estimator, manifest_size);
  size = shm_toc_estimate(&estimator);

  /* Create the shared memory segment, and the TOC for the data in the
     segment. */
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

  /* Pin the segment so that it is not removed even if there are no attached
   * sessions. */
  dsm_pin_segment(seg);

  /* Pin the mapping so that it stays mapped longer than for a single
   * query. Pinning the mapping means that it has no resource
   * owner. */
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

  if (memview_state != NULL)
    return true;

  LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
  memview_state = ShmemInitStruct("memview", sizeof(MemoryViewState), &found);
  if (!found) {
    LWLockInitialize(&memview_state->lock, LWLockNewTrancheId());
    memview_state->handle = memview_dsm_handle();
  }
  LWLockRelease(AddinShmemInitLock);

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

/*
 * Trigger function for inserting a row in the memory view.
 */
Datum memview_insert_row_tgfunc(PG_FUNCTION_ARGS) {
  TriggerData* trigdata;
  char** tgargs;

  Oid owner;
  int owner_attnum;
  Datum owner_value;
  bool owner_isnull;

  Name descr;
  int descr_attnum;
  Datum descr_value;
  bool descr_isnull;

  if (!CALLED_AS_TRIGGER(fcinfo))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called as trigger")));

  trigdata = (TriggerData*)fcinfo->context;
  tgargs = trigdata->tg_trigger->tgargs;

  if (!TRIGGER_FIRED_INSTEAD(trigdata->tg_event))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called as an INSTEAD OF-trigger")));

  if (!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called for each row")));

  if (trigdata->tg_trigger->tgnargs != 2)
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called with 2 parameters, was called with %d",
                    trigdata->tg_trigger->tgnargs),
             errdetail("Trigger need to be created with the column names of "
                       "the row owner and the description text field.")));

  TRACE("tgargs: (%s,%s)", tgargs[0], tgargs[1]);

  owner_attnum = SPI_fnumber(trigdata->tg_relation->rd_att, tgargs[0]);
  owner_value = SPI_getbinval(trigdata->tg_trigtuple,
                              trigdata->tg_relation->rd_att,
                              owner_attnum,
                              &owner_isnull);
  owner = DatumGetObjectId(owner_value);

  descr_attnum = SPI_fnumber(trigdata->tg_relation->rd_att, tgargs[1]);
  descr_value = SPI_getbinval(trigdata->tg_trigtuple,
                              trigdata->tg_relation->rd_att,
                              descr_attnum,
                              &descr_isnull);
  descr = DatumGetName(descr_value);

  memview_row_insert_internal(owner, descr);

  PG_RETURN_POINTER(NULL);
}

Datum memview_row_insert(PG_FUNCTION_ARGS) {
  Oid owner = PG_GETARG_OID(0);
  Name descr = PG_GETARG_NAME(1);

  memview_row_insert_internal(owner, descr);

  PG_RETURN_VOID();
}

void memview_row_insert_internal(Oid owner, Name descr) {
  MemoryViewRecord* record;
  MemoryViewSession* session;

  memview_init_shmem();

  TRACE("inserting row (%d,'%s'::regrole,%s)",
        MyDatabaseId,
        get_role_name(owner),
        NameStr(*descr));

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  record = &session->manifest[session->header->nrecords++];
  record->dboid = MyDatabaseId;
  record->owner = owner;
  namestrcpy(&record->description, NameStr(*descr));
  LWLockRelease(&memview_state->lock);
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

/*
 * Trigger function for deleting a row in the memory view.
 */
Datum memview_delete_row_tgfunc(PG_FUNCTION_ARGS) {
  TriggerData* trigdata;
  int attnum;
  bool isnull;
  Datum value;
  int64 row_id;

  if (!CALLED_AS_TRIGGER(fcinfo))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called as trigger")));

  trigdata = (TriggerData*)fcinfo->context;

  if (!TRIGGER_FIRED_INSTEAD(trigdata->tg_event))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called as an INSTEAD OF-trigger")));

  if (!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called for each row")));

  if (trigdata->tg_trigger->tgnargs != 1)
    ereport(
        ERROR,
        (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
         errmsg("trigger must be created with 1 parameter, was created with %d",
                trigdata->tg_trigger->tgnargs),
         errdetail("Trigger need to be created with the column name of the row "
                   "id.")));

  attnum = SPI_fnumber(trigdata->tg_relation->rd_att,
                       trigdata->tg_trigger->tgargs[0]);
  value = SPI_getbinval(
      trigdata->tg_trigtuple, trigdata->tg_relation->rd_att, attnum, &isnull);

  if (isnull)
    elog(ERROR,
         "attribute \"%s\" cannot be null",
         trigdata->tg_trigger->tgargs[0]);

  row_id = DatumGetInt64(value);

  memview_row_delete_internal(row_id);

  PG_RETURN_POINTER(NULL);
}

Datum memview_row_delete(PG_FUNCTION_ARGS) {
  int32 row_id = PG_GETARG_INT32(0);

  memview_row_delete_internal(row_id);

  PG_RETURN_VOID();
}

void memview_row_delete_internal(int32 row_id) {
  MemoryViewSession* session;

  memview_init_shmem();

  TRACE("deleting row %d", row_id);

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  memmove(&session->manifest[row_id],
          &session->manifest[row_id + 1],
          (session->header->nrecords - row_id) * sizeof(MemoryViewRecord));
  --session->header->nrecords;
  LWLockRelease(&memview_state->lock);
}

/*
 * Trigger function for updating the memory view.
 */
Datum memview_update_row_tgfunc(PG_FUNCTION_ARGS) {
  TriggerData* trigdata;
  char** tgargs;

  int32 row_id;
  int row_id_attnum;
  Datum old_row_id_value, new_row_id_value;
  bool old_row_id_isnull, new_row_id_isnull;

  Oid owner;
  int owner_attnum;
  Datum owner_value;
  bool owner_isnull;

  Name descr;
  int descr_attnum;
  Datum descr_value;
  bool descr_isnull;

  if (!CALLED_AS_TRIGGER(fcinfo))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called as trigger")));

  trigdata = (TriggerData*)fcinfo->context;
  tgargs = trigdata->tg_trigger->tgargs;

  if (!TRIGGER_FIRED_INSTEAD(trigdata->tg_event))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called as an INSTEAD OF-trigger")));

  if (!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
    ereport(ERROR,
            (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
             errmsg("must be called for each row")));

  if (trigdata->tg_trigger->tgnargs != 3)
    ereport(
        ERROR,
        (errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
         errmsg("must be called with 3 parameters, was called with %d",
                trigdata->tg_trigger->tgnargs),
         errdetail("Trigger need to be created with the column name of the row "
                   "id, the column name of the row owner, and the column name "
                   "of the description.")));

  TRACE("tgargs: (%s,%s,%s)", tgargs[0], tgargs[1], tgargs[2]);

  row_id_attnum = SPI_fnumber(trigdata->tg_relation->rd_att, tgargs[0]);
  old_row_id_value = SPI_getbinval(trigdata->tg_trigtuple,
                                   trigdata->tg_relation->rd_att,
                                   row_id_attnum,
                                   &old_row_id_isnull);

  if (old_row_id_isnull)
    elog(ERROR, "attribute \"%s\" cannot be null", tgargs[0]);

  row_id = DatumGetInt64(old_row_id_value);

  /*
   * We don't allow changing the row id for a row, so check that the
   * new tuple has the same value.
   */
  new_row_id_value = SPI_getbinval(trigdata->tg_newtuple,
                                   trigdata->tg_relation->rd_att,
                                   row_id_attnum,
                                   &new_row_id_isnull);

  if (old_row_id_isnull != new_row_id_isnull ||
      DatumGetInt64(old_row_id_value) != DatumGetInt64(new_row_id_value))
    elog(ERROR, "changing value of attribute \"%s\" is not allowed", tgargs[0]);

  owner_attnum = SPI_fnumber(trigdata->tg_relation->rd_att, tgargs[1]);
  owner_value = SPI_getbinval(trigdata->tg_newtuple,
                              trigdata->tg_relation->rd_att,
                              owner_attnum,
                              &owner_isnull);
  owner = DatumGetObjectId(owner_value);

  descr_attnum = SPI_fnumber(trigdata->tg_relation->rd_att, tgargs[2]);
  descr_value = SPI_getbinval(trigdata->tg_newtuple,
                              trigdata->tg_relation->rd_att,
                              descr_attnum,
                              &descr_isnull);
  descr = DatumGetName(descr_value);

  memview_row_update_internal(row_id, owner, descr);

  PG_RETURN_POINTER(NULL);
}

Datum memview_row_update(PG_FUNCTION_ARGS) {
  int32 row_id = PG_GETARG_INT32(0);
  Oid owner = PG_GETARG_OID(1);
  Name descr = PG_GETARG_NAME(2);

  memview_row_update_internal(row_id, owner, descr);

  PG_RETURN_VOID();
}

void memview_row_update_internal(int32 row_id, Oid owner, Name descr) {
  MemoryViewRecord* record;
  MemoryViewSession* session;

  memview_init_shmem();

  TRACE("updating row %d with ('%s'::regrole,%s)",
        row_id,
        get_role_name(owner),
        NameStr(*descr));

  LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);
  session = memview_session_get(memview_state->handle);
  record = &session->manifest[row_id];
  /* No need to change the database OID. It remains the same */
  record->owner = owner;
  namestrcpy(&record->description, NameStr(*descr));
  LWLockRelease(&memview_state->lock);
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
    bool nulls[4] = {0};
    Datum values[4] = {0};
    HeapTuple tuple;
    MemoryViewRecord* record;

    LWLockAcquire(&memview_state->lock, LW_EXCLUSIVE);

    record = &session->manifest[funcctx->call_cntr];

    values[0] = UInt32GetDatum(funcctx->call_cntr);
    values[1] = ObjectIdGetDatum(record->dboid);
    if (record->owner) {
      values[2] = ObjectIdGetDatum(record->owner);
      values[3] = NameGetDatum(&record->description);
    } else {
      nulls[2] = nulls[3] = true;
    }

    tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

    LWLockRelease(&memview_state->lock);

    SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
  }

  SRF_RETURN_DONE(funcctx);
}
