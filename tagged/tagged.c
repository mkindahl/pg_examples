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

#include <executor/executor.h> /* GetAttributeByName */
#include <utils/array.h>       /* ArrayType, PG_GETARG_ARRAYTYPE_P */
#include <utils/builtins.h>    /* interval_out */
#include <utils/datetime.h>    /* Interval, PG_GET_INTERVAL_P */
#include <utils/lsyscache.h>   /* get_rel_name */

PG_MODULE_MAGIC;

enum Tag {
  UNDEF_TAG,
  RANGE_TAG,
  HASH_TAG,
};

typedef struct RangePart {
  int32 vl_len_;
  enum Tag tag;
  NameData attname;
  Interval* interval;
} RangePart;

typedef struct HashPart {
  int32 vl_len_;
  enum Tag tag;
  NameData attname;
  int partitions;
} HashPart;

typedef union Any {
  struct {
    int32 vl_len_;
    enum Tag tag;
  } hdr;
  HashPart hash;
  RangePart range;
} Any;

PG_FUNCTION_INFO_V1(tagged_in);
Datum tagged_in(PG_FUNCTION_ARGS) {
  ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
          errmsg("cannot accept a value of type %s", "tagged"));

  PG_RETURN_VOID(); /* keep compiler quiet */
}

PG_FUNCTION_INFO_V1(tagged_out);
Datum tagged_out(PG_FUNCTION_ARGS) {
  Any* val = (Any*)PG_GETARG_POINTER(0);
  StringInfoData str;

  switch (val->hdr.tag) {
    case HASH_TAG:
      appendStringInfo(&str, "hash/%s/%d", NameStr(val->hash.attname),
                       val->hash.partitions);
      break;

    case RANGE_TAG:
      appendStringInfo(
          &str, "range/%s/%s", NameStr(val->range.attname),
          DatumGetCString(DirectFunctionCall1(
              interval_out, PointerGetDatum(&val->range.interval))));
      break;

    case UNDEF_TAG:
      appendStringInfo(&str, "unknown/%d", val->hdr.tag);
      break;
  }
  PG_RETURN_CSTRING(str.data);
}

PG_FUNCTION_INFO_V1(hash_part);
Datum hash_part(PG_FUNCTION_ARGS) {
  HashPart* part = palloc0(sizeof(HashPart));
  SET_VARSIZE(part, sizeof(HashPart));
  part->tag = HASH_TAG;
  part->partitions = PG_GETARG_INT32(1);
  namestrcpy(&part->attname, NameStr(*PG_GETARG_NAME(0)));
  PG_RETURN_POINTER(part);
}

PG_FUNCTION_INFO_V1(range_part);
Datum range_part(PG_FUNCTION_ARGS) {
  RangePart* part = palloc0(sizeof(RangePart));
  SET_VARSIZE(part, sizeof(RangePart));
  part->tag = RANGE_TAG;
  part->interval = PG_GETARG_INTERVAL_P(1);
  namestrcpy(&part->attname, NameStr(*PG_GETARG_NAME(0)));
  PG_RETURN_POINTER(part);
}

static void process_hash(Oid relid, HashPart* part) {
  AttrNumber attno = get_attnum(relid, NameStr(part->attname));
  if (attno == InvalidAttrNumber)
    ereport(ERROR, errcode(ERRCODE_UNDEFINED_COLUMN),
            errmsg("relation %s does not have an attribute with name %s",
                   get_rel_name(relid), NameStr(part->attname)));

  elog(NOTICE, "hash: rel='%s', attname='%s', attno=%d, partitions=%d",
       get_rel_name(relid), NameStr(part->attname), attno, part->partitions);
}

static void process_range(Oid relid, RangePart* part) {
  AttrNumber attno = get_attnum(relid, NameStr(part->attname));
  if (attno == InvalidAttrNumber)
    ereport(ERROR, errcode(ERRCODE_UNDEFINED_COLUMN),
            errmsg("relation %s does not have an attribute with name %s",
                   get_rel_name(relid), NameStr(part->attname)));

  elog(NOTICE, "range: rel='%s', attname='%s', interval='%s'",
       get_rel_name(relid), NameStr(part->attname),
       DatumGetCString(
           DirectFunctionCall1(interval_out, PointerGetDatum(part->interval))));
}

static void process_undef(Oid relid, Any* any) {
  elog(NOTICE, "undef: rel=%s, tag=%d", get_rel_name(relid), any->hdr.tag);
}

static void process_any(Oid relid, Any* val) {
  switch (val->hdr.tag) {
    case HASH_TAG:
      process_hash(relid, &val->hash);
      break;

    case RANGE_TAG:
      process_range(relid, &val->range);
      break;

    case UNDEF_TAG:
      process_undef(relid, val);
      break;
  }
}

PG_FUNCTION_INFO_V1(builder_elem);
Datum builder_elem(PG_FUNCTION_ARGS) {
  process_any(PG_GETARG_OID(0), (Any*)PG_GETARG_POINTER(1));
  PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(builder_array);
Datum builder_array(PG_FUNCTION_ARGS) {
  int16 typlen;
  bool typbyval;
  char typalign;
  Datum* datum;
  bool* isnull;
  int i, nelems;
  Oid relid = PG_GETARG_OID(0);
  ArrayType* array = PG_GETARG_ARRAYTYPE_P(1);

  get_typlenbyvalalign(ARR_ELEMTYPE(array), &typlen, &typbyval, &typalign);
  deconstruct_array(array, ARR_ELEMTYPE(array), typlen, typbyval, typalign,
                    &datum, &isnull, &nelems);
  for (i = 0; i < nelems; i++) {
    Any* val = (Any*)DatumGetByteaP(datum[i]);
    process_any(relid, val);
  }
  PG_RETURN_VOID();
}
