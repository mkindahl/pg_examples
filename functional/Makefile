MODULES = functional

REGRESS = functional

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

functional.o: functional.c
