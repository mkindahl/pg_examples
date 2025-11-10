#include "debug.h"

#include <postgres.h>
#include <fmgr.h>

#include <executor/tuptable.h>
#include <utils/lsyscache.h>

/* Call the output function for a value given the type and append the string
 * representation to the provided buffer. */
static void appendStringInfoValueOut(StringInfo buf, Oid typid, Datum value,
                                     bool isnull) {
  Oid typoutputfunc;
  bool typIsVarlena;
  FmgrInfo typoutputfinfo;

  getTypeOutputInfo(typid, &typoutputfunc, &typIsVarlena);
  fmgr_info(typoutputfunc, &typoutputfinfo);
  if (isnull)
    appendStringInfoString(buf, "NULL");
  else
    appendStringInfoString(buf, OutputFunctionCall(&typoutputfinfo, value));
}

const char* SlotToString(TupleTableSlot* slot) {
  StringInfoData info;

  slot_getallattrs(slot);

  initStringInfo(&info);

  appendStringInfoString(&info, "(");
  for (int i = 0; i < slot->tts_tupleDescriptor->natts; ++i) {
    const Form_pg_attribute att = TupleDescAttr(slot->tts_tupleDescriptor, i);
    appendStringInfoValueOut(&info, att->atttypid, slot->tts_values[i],
                             slot->tts_isnull[i]);
    if (i + 1 < slot->tts_tupleDescriptor->natts)
      appendStringInfoString(&info, ", ");
  }
  appendStringInfoString(&info, ")");
  return info.data;
}
