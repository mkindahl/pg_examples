MODULE_big = functional
OBJS = functional.o sorting.o debug.o

VERSION_functional = $(shell perl -ne 'print "$$1" if /^default_version.*(\d+\.\d+)/' functional.control)

EXTENSION = functional
DATA_built = functional--$(VERSION_functional).sql
REGRESS = apply sorted
REGRESS_OPTS += --load-extension=functional

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

functional--$(VERSION_functional).sql: functional.sql
	cp $< $@

functional.o: functional.c
