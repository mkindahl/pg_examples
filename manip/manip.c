#include <postgres.h>
#include <fmgr.h>

#include <miscadmin.h>

#include <access/relscan.h>
#include <access/table.h>
#include <access/tableam.h>
#include <executor/tuptable.h>
#include <utils/lsyscache.h>
#include <utils/snapmgr.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(scan_table);
Datum scan_table(PG_FUNCTION_ARGS) {
  Oid reloid = PG_GETARG_OID(0);
  Relation rel = table_open(reloid, AccessShareLock);
  TableScanDesc scan = table_beginscan(rel, GetActiveSnapshot(), 0, NULL);
  TupleTableSlot *slot = table_slot_create(rel, NULL);
  TupleDesc tupleDesc = RelationGetDescr(rel);
  int count = 0;
  while (table_scan_getnextslot(scan, ForwardScanDirection, slot)) {
    CHECK_FOR_INTERRUPTS();
    slot_getallattrs(slot);
    if (count++ < 10) {
      for (int i = 0; i < slot->tts_nvalid; ++i) {
        Form_pg_attribute attr = TupleDescAttr(tupleDesc, i);
        char *str;
        if (attr->attisdropped)
          continue;
        if (slot->tts_isnull[i])
          str = "NULL";
        else {
          Oid typoutputfunc;
          bool typIsVarlena;
          FmgrInfo typoutputfinfo;
          getTypeOutputInfo(attr->atttypid, &typoutputfunc, &typIsVarlena);
          fmgr_info(typoutputfunc, &typoutputfinfo);
          str = OutputFunctionCall(&typoutputfinfo, slot->tts_values[i]);
        }

        elog(NOTICE,
             "attnum: %d, attname: %s, value: %s",
             attr->attnum,
             NameStr(attr->attname),
             str);
      }
    }
  }
  if (count >= 10)
    elog(NOTICE, "... and %d more rows", count - 10);
  ExecDropSingleTupleTableSlot(slot);
  table_endscan(scan);
  table_close(rel, NoLock);
  PG_RETURN_VOID();
}
