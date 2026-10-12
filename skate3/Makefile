# Native build + tests for the full-runtime host (s3_halo.h). The web build compiles the same sources
# through engine/tools/web_build.py. First build: ~690 files, ~10 min at -j4.
CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -ffp-contract=off -g0 -w
OBJ := build/obj
LIB_SRC := $(sort $(shell find src/s3/core src/s3/game -name '*.cpp') $(wildcard src/s3/data/*.cpp)) src/halo_host.cpp
LIB_OBJ := $(patsubst %.cpp,$(OBJ)/%.o,$(LIB_SRC))
PACK ?= $(HOME)/Documents/haloskate-ref/cleanpack/core.pack

$(OBJ)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -Isrc -Iinclude -MMD -MP -c $< -o $@

-include $(LIB_OBJ:.o=.d) $(OBJ)/tests/halo_host_test.d

$(OBJ)/libs3halo.a: $(LIB_OBJ)
	@rm -f $@
	@ar rcs $@ $^

build/halo_host_test: $(OBJ)/tests/halo_host_test.o $(OBJ)/libs3halo.a
	$(CXX) $(CXXFLAGS) $^ -o $@

lib: $(OBJ)/libs3halo.a

test: build/halo_host_test
	./build/halo_host_test $(PACK) $(TESTARGS)

clean:
	rm -rf build

.PHONY: lib test clean
