#ifndef TRACE_DEBUG_H_
#define TRACE_DEBUG_H_

#include <postgres.h>

#include <executor/tuptable.h>

static inline const char* boolstr(bool val) { return val ? "true" : "false"; }

extern const char* SlotToString(TupleTableSlot* slot);

#endif /* TRACE_DEBUG_H_ */
