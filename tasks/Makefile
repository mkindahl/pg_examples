MODULE_big = tasks
OBJS = tasks.o

prog = perl -ne 'print "$$1" if /default_version = '\''(\d+\.\d+)'\''/' tasks.control
VERSION_tasks = $(shell $(prog))

EXTENSION = tasks
DATA = tasks--$(VERSION_tasks).sql
PGFILEDESC = "tasks - task scheduler with tasks queue"
PG_CFLAGS = -std=c99
PG_CPPFLAGS = -Isrc

REGRESS = bytes
REGRESS_OPTS += --load-extension=tasks

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
