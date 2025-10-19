#include <postgres.h>
#include <fmgr.h>

#include "c.h"

#include <storage/dsm.h>

#define MEMVIEW_MAX_RECORDS 100

/*
 * Memory view record with some example data.
 *
 * This is just an example of a memory view record that can be shared
 * between different backends (or even the background worker).
 *
 * We have added an owner to be able to play around with row-level
 * security to show only permitted rows by defining a view on top of
 * the set-returning function to read the records.
 */
typedef struct MemoryViewRecord {
  Oid dboid;
  Oid owner;
  NameData description;
} MemoryViewRecord;

typedef struct MemoryViewHeader {
  size_t nrecords;
} MemoryViewHeader;

/*
 * A memory view session.
 *
 * This is the session's memory view exists for the duration of the
 * running session.
 */
typedef struct MemoryViewSession {
  dsm_segment* segment;

  MemoryViewHeader* header;
  MemoryViewRecord* manifest;
} MemoryViewSession;

extern PGDLLEXPORT Datum memview_row_delete(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_row_insert(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_row_update(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_view_reset(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_view_scan(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_delete_row_tgfunc(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_insert_row_tgfunc(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum memview_update_row_tgfunc(PG_FUNCTION_ARGS);

extern MemoryViewSession* memview_session_get(dsm_handle handle);
extern dsm_handle memview_dsm_handle(void);
