CXX ?= c++
AR ?= ar
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
LDLIBS += -lmpfr -lgmpxx -lgmp
PREFIX ?= /usr/local
WITH_READLINE ?= 1
ifeq ($(WITH_READLINE),1)
CLI_CPPFLAGS += -DCLICALC_READLINE $(shell pkg-config --cflags readline 2>/dev/null)
CLI_LDLIBS += -lreadline
endif
CORE = number parser functions units engine session
OBJECTS = $(addprefix build/,$(addsuffix .o,$(CORE)))

.PHONY: all test clean install FORCE
all: build/clicalc

build:
	mkdir -p build
build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/cli-config: FORCE | build
	@printf '%s\n' '$(WITH_READLINE)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
build/main.o: src/main.cpp build/cli-config Makefile | build
	$(CXX) $(CPPFLAGS) $(CLI_CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/terminal.o: src/terminal.cpp build/cli-config Makefile | build
	$(CXX) $(CPPFLAGS) $(CLI_CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/libclicalc.a: $(OBJECTS)
	$(AR) rcs $@ $^
build/clicalc: build/main.o build/terminal.o build/libclicalc.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) $(CLI_LDLIBS) -o $@
build/engine_tests: tests/engine_tests.cpp build/libclicalc.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
build/features_tests: tests/features_tests.cpp build/libclicalc.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
test: build/clicalc build/engine_tests build/features_tests
	./build/engine_tests
	./build/features_tests
	python3 tests/cli_tests.py ./build/clicalc
ifeq ($(WITH_READLINE),1)
	python3 tests/terminal_tests.py ./build/clicalc
endif
install: build/clicalc
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/clicalc $(DESTDIR)$(PREFIX)/bin/clicalc
clean:
	$(RM) -r build
-include $(wildcard build/*.d)
