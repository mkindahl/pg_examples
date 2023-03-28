MODULES = scanner
EXTENSION = simple scanner
DATA = simple--1.0.sql simple--1.0--2.0.sql scanner--0.1.sql

REGRESS = basic update

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

