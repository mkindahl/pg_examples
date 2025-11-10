#include <postgres.h>
#include <fmgr.h>

#include <access/heapam.h>
#include <access/heaptoast.h>
#include <access/htup.h>
#include <access/skey.h>
#include <access/tableam.h>
#include <access/tupdesc.h>
#include <commands/vacuum.h>
#include <executor/tuptable.h>
#include <funcapi.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/tuplesort.h>

#include "debug.h"

PG_FUNCTION_INFO_V1(sorted_by_index);

/*
 * Sort the actual tuples.
 */
static Tuplesortstate* tuplesort_by_index(Relation rel, Relation idx) {
  TupleDesc tupdesc = RelationGetDescr(rel);
  TupleTableSlot* slot = table_slot_create(rel, NULL);
  BufferHeapTupleTableSlot* hslot = (BufferHeapTupleTableSlot*)slot;
  IndexScanDesc indexScan;
  Tuplesortstate* tuplesort;
  VacuumParams params;
  struct VacuumCutoffs cutoffs;

  tuplesort = tuplesort_begin_cluster(
      tupdesc, idx, maintenance_work_mem, NULL, TUPLESORT_NONE);
  indexScan = index_beginscan(rel, idx, SnapshotAny, NULL, 0, 0);

  index_rescan(indexScan, NULL, 0, NULL, 0);

  memset(&params, 0, sizeof(VacuumParams));
  vacuum_get_cutoffs(rel, &params, &cutoffs);

  /*
   * Iterate over the index, but it can point to dead tuples so we
   * have to check if the tuple is dead before actually adding it to
   * the heapsort.
   */
  while (index_getnext_slot(indexScan, ForwardScanDirection, slot)) {
    Buffer buf;
    HTSV_Result result;
    HeapTuple tuple;

    CHECK_FOR_INTERRUPTS();

    tuple = ExecFetchSlotHeapTuple(slot, false, NULL);
    buf = hslot->buffer;

    LockBuffer(buf, BUFFER_LOCK_SHARE);
    result = HeapTupleSatisfiesVacuum(tuple, cutoffs.OldestXmin, buf);
    LockBuffer(buf, BUFFER_LOCK_UNLOCK);

    if (result == HEAPTUPLE_DEAD)
      continue;

    tuplesort_putheaptuple(tuplesort, tuple);
  }

  index_endscan(indexScan);
  ExecDropSingleTupleTableSlot(slot);

  tuplesort_performsort(tuplesort);

  return tuplesort;
}

static HeapTuple rewrite_tuple(HeapTuple tuple, TupleDesc tupdesc) {
  Datum* values = (Datum*)palloc(tupdesc->natts * sizeof(Datum));
  bool* isnull = (bool*)palloc(tupdesc->natts * sizeof(bool));

  /*
   * We need to use heap_form_tuple() to satisfy the table-function
   * protocol, so we get all values from the heap tuple and read the
   * values from the slot data.
   */
  heap_deform_tuple(tuple, tupdesc, values, isnull);

  /* Set all dropped columns to null. Mostly a precaution. */
  for (int i = 0; i < tupdesc->natts; i++) {
    if (TupleDescCompactAttr(tupdesc, i)->attisdropped)
      isnull[i] = true;
  }

  return heap_form_tuple(tupdesc, values, isnull);
}

/*
 * Function that returns a sorted result set based on an index.
 *
 * Intended as an example for how to use tuple-sort with clustering.
 */
Datum sorted_by_index(PG_FUNCTION_ARGS) {
  Oid reloid = PG_GETARG_OID(0);
  Oid idxoid = PG_GETARG_OID(1);
  Relation rel = table_open(reloid, AccessShareLock);
  Relation idx = index_open(idxoid, AccessExclusiveLock);

  Tuplesortstate* tuplesort;
  FuncCallContext* funcctx;
  TupleDesc tupdesc;
  HeapTuple tuple;

  if (SRF_IS_FIRSTCALL()) {
    MemoryContext oldcontext;

    funcctx = SRF_FIRSTCALL_INIT();

    oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    funcctx->tuple_desc = BlessTupleDesc(RelationGetDescr(rel));
    funcctx->user_fctx = tuplesort_by_index(rel, idx);

    MemoryContextSwitchTo(oldcontext);
  }

  index_close(idx, NoLock);
  table_close(rel, NoLock);

  funcctx = SRF_PERCALL_SETUP();

  tuplesort = funcctx->user_fctx;
  tupdesc = funcctx->tuple_desc;

  tuple = tuplesort_getheaptuple(tuplesort, true);

  if (tuple)
    SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(rewrite_tuple(tuple, tupdesc)));

  tuplesort_end(tuplesort);
  SRF_RETURN_DONE(funcctx);
}
