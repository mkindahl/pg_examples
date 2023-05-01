MODULES = manip reporter tagged
EXTENSION = simple manip reporter tagged
DATA = simple--1.0.sql simple--1.0--2.0.sql manip--0.1.sql reporter--0.1.sql tagged--0.1.sql

REGRESS = basic update tagged scanner

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

reporter.o: reporter.c
manip.o: manip.c
reporter.o: reporter.c
