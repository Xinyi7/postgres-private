# This Makefile uses the PGXS build system,
# see https://www.postgresql.org/docs/15/extend-pgxs.html for details.

# In general, you should glance over the actual compile commands printed by "make install -j"
# to confirm that you're compiling with the right flags.
# You are also not required to use PGXS, feel free to replace it with your own build system as
# long as running "make install -j" installs an extension named "db721_fdw" with the same API.

EXTENSION = db721_fdw
MODULE_big = db721_fdw
DATA = db721_fdw--0.0.1.sql
OBJS = src/dog.o src/db721_fdw_impl.o src/db721_fdw.o
SHLIB_LINK = -lstdc++

# If PG_CONFIG is not set, try the default NoisePage build folder first, then try PATH.
PG_CONFIG ?= ../../../build/postgres/bin/pg_config
PG_CONFIG ?= pg_config

override PG_CXXFLAGS += -std=c++17 		# We generally use C++17.
override PG_CXXFLAGS += -Wno-register 	# PostgreSQL uses the register storage class.

# From "man gcc",
# > If you use multiple -O options, with or without level numbers, the last such option is the one that is effective.
# You might want to try one of the following options:
# override PG_CXXFLAGS += -O0 -ggdb
# override PG_CXXFLAGS += -O3

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
