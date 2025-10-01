MODULES = memview
EXTENSION = memview

VERSION_memview = $(shell perl -ne 'print "$$1" if /^default_version.*(\d+\.\d+)/' tasks.control)
dist-name = postgresql-pg-lsm-$(package-version)
dist-file = $(dist-name).tar.gz

DATA_built = memview--$(package-version).sql

REGRESS = basic
REGRESS_OPTS += --load-extension=memview

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# .gitattributes make sure that we do not include .github and other
# directories that are part of the repository CI/CD.
dist:
	git archive --prefix=$(dist-name)/ --format=tar.gz -o $(dist-file) HEAD

memview--$(VERSION_memview).sql: memview.sql
	cp $< $@

memview.o: memview.c
