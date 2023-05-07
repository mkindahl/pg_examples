MODULES = manip reporter cplusplus
EXTENSION = simple manip reporter cplusplus
DATA = simple--1.0.sql simple--1.0--2.0.sql manip--0.1.sql reporter--0.1.sql tagged--0.1.sql cplusplus--0.1.sql

REGRESS = basic update tagged manip cplusplus

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

manip.o: manip.c
reporter.o: reporter.c

CXXFLAGS += -fno-exceptions -fno-rtti -Wno-write-strings -Wno-register -Wno-deprecated-register -Iinclude/c++

cplusplus.o: cplusplus.cpp include/c++/pgexxt/scanner.h

cplusplus.bc: CPPFLAGS += $(CXXFLAGS)
