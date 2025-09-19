MODULES = memview
EXTENSION = memview

#package-version = $(shell git describe --long --match="v[0-9]*" | cut -d- -f1 | sed 's/^v//')
package-version = 0.1
dist-name = postgresql-pg-lsm-$(package-version)
dist-file = $(dist-name).tar.gz

DATA_built = memview--$(package-version).sql

REGRESS = basic

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# .gitattributes make sure that we do not include .github and other
# directories that are part of the repository CI/CD.
dist:
	git archive --prefix=$(dist-name)/ --format=tar.gz -o $(dist-file) HEAD

memview--$(package-version).sql: memview.sql
	cp $< $@

memview.o: memview.c
