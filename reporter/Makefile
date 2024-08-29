MODULES = reporter
EXTENSION = reporter
DATA = reporter--0.1.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

reporter.o: reporter.c
