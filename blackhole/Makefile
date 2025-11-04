EXTENSION = blackhole

VERSION_blackhole = $(shell perl -ne 'print "$$1" if /^default_version.*(\d+\.\d+)/' blackhole.control)

EXTENSION = blackhole
DATA_built = blackhole--$(VERSION_blackhole).sql
PGFILEDESC = "blackhole - Blackhole table access method"

REGRESS = basic

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

blackhole--$(VERSION_blackhole).sql: blackhole.sql
	cp $< $@
