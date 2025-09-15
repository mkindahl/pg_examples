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

#include <postgres.h>
#include <fmgr.h>

#include <miscadmin.h>
#include <parser/parse_func.h>
#include <utils/regproc.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(apply);

static NullableDatum OidInvokeFunctionCall(Oid func_oid, Oid collation,
                                           int nargs, NullableDatum* args) {
  /* We cannot use LOCAL_FCINFO here since it expects a constant number of
   * arguments, so we need to allocate space for fcinfo using palloc(). */
  FunctionCallInfo fcinfo = palloc(SizeForFunctionCallInfo(nargs));
  FmgrInfo flinfo;
  NullableDatum result;
  Datum value;

  fmgr_info(func_oid, &flinfo);

  /* Just return if the function is strict */
  if (flinfo.fn_strict)
    return (NullableDatum){
        .isnull = true,
        .value = 0,
    };

  InitFunctionCallInfoData(*fcinfo, &flinfo, nargs, collation, NULL, NULL);
  for (int i = 0; i < nargs; ++i) fcinfo->args[i] = args[i];

  value = FunctionCallInvoke(fcinfo);

  result.isnull = fcinfo->isnull;
  result.value = value;

  pfree(fcinfo);

  return result;
}

/*
 * Apply another function to the parameters.
 */
Datum apply(PG_FUNCTION_ARGS) {
  Oid funcoid;
  NullableDatum result;
  int nargs = PG_NARGS() - 1;
  Name funcname = PG_GETARG_NAME(0);
  List* namelist =
      stringToQualifiedNameList(NameStr(*funcname), fcinfo->context);

  /* Build a type array and use that to look up the correct function by name */
  Oid* funcargtypes = palloc_array(Oid, nargs);
  for (int i = 1; i < PG_NARGS(); ++i)
    funcargtypes[i - 1] = get_fn_expr_argtype(fcinfo->flinfo, i);
  funcoid = LookupFuncName(namelist, nargs, funcargtypes, false);
  pfree(funcargtypes);

  /* Call the function and get the result */
  result = OidInvokeFunctionCall(funcoid, InvalidOid, nargs, &fcinfo->args[1]);

  if (result.isnull)
    PG_RETURN_NULL();
  PG_RETURN_DATUM(result.value);
}
