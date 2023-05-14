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
#include "pgexxt/table.h"

Datum scan_table(PG_FUNCTION_ARGS) {
  Oid reloid = PG_GETARG_OID(0);
  pgexxt::Table::Handle handle(reloid, AccessShareLock);
  TupleDesc tupleDesc = handle.GetTupleDesc();
  int count = 0;
  for (auto slot : handle.ForwardScan()) {
    CHECK_FOR_INTERRUPTS();
    slot_getallattrs(slot);
    if (count++ < 10) {
      for (auto attr : handle.attributes()) {
        char* str;
        if (attr.is_dropped())
          continue;
        if (slot.isnull(attr))
          str = "NULL";
        else {
          Oid typoutputfunc;
          bool typIsVarlena;
          FmgrInfo typoutputfinfo;
          getTypeOutputInfo(attr->atttypid, &typoutputfunc, &typIsVarlena);
          fmgr_info(typoutputfunc, &typoutputfinfo);
          str = OutputFunctionCall(&typoutputfinfo, slot.value(attr));
        }

        elog(NOTICE, "attnum: %d, attname: %s, value: %s", attr->attnum,
             NameStr(attr->attname), str);
      }
    }
  }
  if (count >= 10)
    elog(NOTICE, "... and %d more rows", count - 10);
  PG_RETURN_VOID();
}
