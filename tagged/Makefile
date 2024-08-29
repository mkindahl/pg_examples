MODULES = tagged
EXTENSION = tagged
DATA = tagged--0.1.sql

REGRESS = tagged

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

tagged.o: tagged.c
