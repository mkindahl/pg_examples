MODULES = aggs
EXTENSION = aggs
DATA = aggs--0.1.sql

REGRESS = aggs

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

aggs.o: aggs.c
