extern "C" {
#include <postgres.h>
#include <fmgr.h>

#include <access/relscan.h>
#include <access/table.h>
#include <access/tableam.h>
#include <executor/tuptable.h>
#include <miscadmin.h>
#include <utils/lsyscache.h>
#include <utils/snapmgr.h>

PG_MODULE_MAGIC;

// We declare the functions inside the extern "C" since this will
// make sure that they stay that way even when we leave the scope.
PG_FUNCTION_INFO_V1(scan_table);
Datum scan_table(PG_FUNCTION_ARGS);
}

#include "pgexxt/scanner.h"

Datum scan_table(PG_FUNCTION_ARGS) {
  Oid reloid = PG_GETARG_OID(0);
  Relation rel = table_open(reloid, AccessShareLock);
  TupleDesc tupleDesc = RelationGetDescr(rel);
  int count = 0;
  pgexxt::ForwardScanner scanner(rel);
  for (auto slot : scanner) {
    CHECK_FOR_INTERRUPTS();
    slot_getallattrs(slot);
    if (count++ < 10) {
      for (int i = 0; i < slot->tts_nvalid; ++i) {
        Form_pg_attribute attr = TupleDescAttr(tupleDesc, i);
        char* str;
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

        elog(NOTICE, "attnum: %d, attname: %s, value: %s", attr->attnum,
             NameStr(attr->attname), str);
      }
    }
  }
  if (count >= 10)
    elog(NOTICE, "... and %d more rows", count - 10);
  table_close(rel, NoLock);
  PG_RETURN_VOID();
}
