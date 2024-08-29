MODULES = manip
EXTENSION = manip
DATA = manip--0.1.sql

REGRESS = scanner

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

manip.o: manip.c
