#include <postgres.h>
#include <fmgr.h>

#include <funcapi.h>
#include <miscadmin.h>

#include <access/relscan.h>
#include <access/table.h>
#include <access/tableam.h>
#include <catalog/pg_type.h>
#include <executor/tuptable.h>
#include <utils/lsyscache.h>
#include <utils/snapmgr.h>
#include <utils/syscache.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(my_array_agg_dropfn);
PG_FUNCTION_INFO_V1(my_array_agg_transfn);
PG_FUNCTION_INFO_V1(my_array_agg_finalfn);

static ArrayBuildState *dropArrayResult(ArrayBuildState *astate,
                                        Oid element_type,
                                        MemoryContext rcontext) {
  Assert(astate != NULL);
  Assert(astate->element_type == element_type);
  for (int i = 0; i < astate->nelems - 1; ++i) {
    astate->dvalues[i] = astate->dvalues[i + 1];
    astate->dnulls[i] = astate->dnulls[i + 1];
  }
  astate->nelems--;
  return astate;
}

static void Print(const char *func, FunctionCallInfo fcinfo) {
  StringInfoData string;
  initStringInfo(&string);
  appendStringInfoString(&string, "(");
  for (int i = 0; i < fcinfo->nargs; i++) {
    Form_pg_type typtup;
    const char *string_value;
    Oid arg_typeid = get_fn_expr_argtype(fcinfo->flinfo, i);
    HeapTuple tp = SearchSysCache1(TYPEOID, ObjectIdGetDatum(arg_typeid));
    if (!HeapTupleIsValid(tp))
      elog(ERROR, "cache lookup failed for type %u", arg_typeid);
    typtup = (Form_pg_type)GETSTRUCT(tp);
    if (PG_ARGISNULL(i)) {
      string_value = "NULL";
    } else if (arg_typeid != INTERNALOID) {
      Oid typoutputfunc;
      bool typIsVarlena;
      FmgrInfo typoutputfinfo;
      getTypeOutputInfo(arg_typeid, &typoutputfunc, &typIsVarlena);
      fmgr_info(typoutputfunc, &typoutputfinfo);
      string_value = OutputFunctionCall(&typoutputfinfo, PG_GETARG_DATUM(i));
    } else {
      string_value = "<INTERNAL>";
    }

    if (i > 0)
      appendStringInfoString(&string, ", ");
    appendStringInfo(
        &string, "'%s'::%s", string_value, NameStr(typtup->typname));
    ReleaseSysCache(tp);
  }
  appendStringInfoString(&string, ")");
  elog(NOTICE, "%s: %s", func, string.data);
}

Datum my_array_agg_transfn(PG_FUNCTION_ARGS) {
  Oid arg1_typeid = get_fn_expr_argtype(fcinfo->flinfo, 1);
  MemoryContext aggcontext;
  ArrayBuildState *state;
  Datum elem;

  Print(__func__, fcinfo);
  if (arg1_typeid == InvalidOid)
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("could not determine input data type")));

  /*
   * Note: we do not need a run-time check about whether arg1_typeid is a
   * valid array element type, because the parser would have verified that
   * while resolving the input/result types of this polymorphic aggregate.
   */

  if (!AggCheckCallContext(fcinfo, &aggcontext)) {
    /* cannot be called directly because of internal-type argument */
    elog(ERROR, "array_agg_transfn called in non-aggregate context");
  }

  if (PG_ARGISNULL(0))
    state = initArrayResult(arg1_typeid, aggcontext, false);
  else
    state = (ArrayBuildState *)PG_GETARG_POINTER(0);

  elem = PG_ARGISNULL(1) ? (Datum)0 : PG_GETARG_DATUM(1);

  state =
      accumArrayResult(state, elem, PG_ARGISNULL(1), arg1_typeid, aggcontext);

  /*
   * The transition type for array_agg() is declared to be "internal", which
   * is a pass-by-value type the same size as a pointer.  So we can safely
   * pass the ArrayBuildState pointer through nodeAgg.c's machinations.
   */
  PG_RETURN_POINTER(state);
}

Datum my_array_agg_finalfn(PG_FUNCTION_ARGS) {
  Datum result;
  ArrayBuildState *state =
      PG_ARGISNULL(0) ? NULL : (ArrayBuildState *)PG_GETARG_POINTER(0);
  int dims[1] = {state->nelems};
  int lbs[1] = {1};

  Print(__func__, fcinfo);
  Assert(AggCheckCallContext(fcinfo, NULL));

  if (!state)
    PG_RETURN_NULL();

  result = makeMdArrayResult(state, 1, dims, lbs, CurrentMemoryContext, false);

  PG_RETURN_DATUM(result);
}

Datum my_array_agg_dropfn(PG_FUNCTION_ARGS) {
  MemoryContext aggcontext;
  Oid arg1_typeid = get_fn_expr_argtype(fcinfo->flinfo, 1);
  ArrayBuildState *state = PG_ARGISNULL(0)
                               ? initArrayResult(arg1_typeid, aggcontext, false)
                               : (ArrayBuildState *)PG_GETARG_POINTER(0);

  Print(__func__, fcinfo);

  if (arg1_typeid == InvalidOid)
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("could not determine input data type")));

  if (!AggCheckCallContext(fcinfo, &aggcontext))
    elog(ERROR, "array_agg_transfn called in non-aggregate context");

  PG_RETURN_POINTER(dropArrayResult(state, arg1_typeid, aggcontext));
}
