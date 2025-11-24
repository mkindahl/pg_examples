MODULE_big = tasks
OBJS = tasks.o

VERSION_tasks = $(shell perl -ne 'print "$$1" if /^default_version.*(\d+\.\d+)/' tasks.control)

EXTENSION = tasks
DATA_built = tasks--$(VERSION_tasks).sql
PGFILEDESC = "tasks - task workers with tasks queue"
PG_CFLAGS = -std=c99
PG_CPPFLAGS = -Isrc

REGRESS = bytes
REGRESS_OPTS += --load-extension=tasks

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

tasks--$(VERSION_tasks).sql: tasks.sql
	cp $< $@

tasks.o: tasks.c tasks.h
