MAKEFLAGS += --no-builtin-rules
export LANG=C LC_ALL=C

OBJ_DIRS := $(wildcard obj cross/*/obj)
BIN_DIRS := $(wildcard bin cross/*/bin)
BUILD_DIRS := $(wildcard gen) $(OBJ_DIRS) $(BIN_DIRS)

-include $(foreach obj,$(OBJ_DIRS),$(patsubst src/%.c,$(obj)/%.d,$(wildcard src/*.c)))

.PHONY: all clean test _clean _nop
.PRECIOUS: obj/%.o

ifneq ($(origin CROSS_TRIPLE), undefined)
CROSS_PFX = /dev/shm/$(CROSS_TRIPLE)-cross/bin/$(CROSS_TRIPLE)-
endif

CC ?= gcc
ifeq ($(origin CC), default)
CC = gcc
endif

CXX ?= g++
ifeq ($(origin CXX), default)
CXX = g++
endif

CPP ?= cpp
ifeq ($(origin CPP), default)
CPP = cpp
endif

AR ?= ar
ifeq ($(origin AR), default)
AR = ar
endif

AS ?= as
ifeq ($(origin AS), default)
AS = as
endif

LD ?= ld
ifeq ($(origin LD), default)
LD = ld
endif

NM ?= nm
RANLIB ?= ranlib
STRIP ?= strip

CROSS_CC ?= $(CROSS_PFX)$(CC)
CROSS_CXX ?= $(CROSS_PFX)$(CXX)
CROSS_CPP ?= $(CROSS_PFX)$(CPP)
CROSS_AR ?= $(CROSS_PFX)$(AR)
CROSS_AS ?= $(CROSS_PFX)$(AS)
CROSS_LD ?= $(CROSS_PFX)$(LD)
CROSS_NM ?= $(CROSS_PFX)$(NM)
CROSS_RANLIB ?= $(CROSS_PFX)$(RANLIB)
CROSS_STRIP ?= $(CROSS_PFX)$(STRIP)

STRIP_ARGS ?= -s -R .comment -R .hash -R .gnu.hash

CSTD ?= c17
CXXSTD ?= c++17
WFLAGS ?= -Wall -Wextra -Wimplicit-fallthrough -pedantic

CPPFLAGS ?= -MMD -MP
CFLAGS ?= -O2 -ggdb
CXXFLAGS ?= $(CFLAGS)
override CFLAGS += -std=$(CSTD) $(WFLAGS)
override CXXFLAGS += -std=$(CXXSTD) $(WFLAGS)
LDFLAGS ?=

CROSS_CPPFLAGS ?= $(CPPFLAGS)
CROSS_CFLAGS ?= -Os -ggdb -static \
	-Igen \
	-ffunction-sections \
	-flto -fdata-sections -Wl,--gc-sections
CROSS_CXXFLAGS ?= $(CXXFLAGS)
override CROSS_CFLAGS += -std=$(CSTD) $(WFLAGS)
override CROSS_CXXFLAGS += -std=$(CXXSTD) $(WFLAGS)
CROSS_LDFLAGS ?=

COMPILE = $(CC) $(CPPFLAGS) $(CFLAGS)
CROSS_COMPILE = $(CROSS_CC) $(CROSS_CPPFLAGS) $(CROSS_CFLAGS)


# TODO maybe? Dependency generation:
# override CFLAGS += -MMD -MP
# -include $(patsubst src/%.c,obj/*.d,$(wildcard src/*.c))

# generic build rules
cross/$(CROSS_TRIPLE)/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CROSS_COMPILE) -c $< -o $@

obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(COMPILE) -c $< -o $@


cross/$(CROSS_TRIPLE)/bin/%: cross/$(CROSS_TRIPLE)/obj/%.o
	@mkdir -p $(@D)
	$(CROSS_COMPILE) $< $(CROSS_LDFLAGS) -o $@

bin/%: obj/%.o
	@mkdir -p $(@D)
	$(COMPILE) $< $(LDFLAGS) -o $@


# we may want to strip cross-compiled size binaries for size
cross/$(CROSS_TRIPLE)/bin/%-stripped: cross/$(CROSS_TRIPLE)/bin/%
	$(CROSS_STRIP) $(STRIP_ARGS) -o $@ $<


UGETTY_OBJS := ugetty.o ugetty_args.o bnprintf.o debugp.o proxy_io.o util.o

# specific build rules
cross/$(CROSS_TRIPLE)/bin/ugetty: $(addprefix cross/$(CROSS_TRIPLE)/obj/,$(UGETTY_OBJS))
	@mkdir -p $(@D)
	$(CROSS_COMPILE) $^ $(CROSS_LDFLAGS) -o $@

bin/ugetty: $(addprefix obj/,$(UGETTY_OBJS))
	@mkdir -p $(@D)
	$(COMPILE) $^ $(LDFLAGS) -o $@

bin/proxycommand: $(addprefix obj/,proxycommand.o bnprintf.o debugp.o proxy_io.o util.o)
	@mkdir -p $(@D)
	$(COMPILE) $^ $(LDFLAGS) -o $@

bin/test_parse_args: $(addprefix obj/,test_parse_args.o ugetty_args.o bnprintf.o util.o)
	@mkdir -p $(@D)
	$(COMPILE) $^ $(LDFLAGS) -o $@

test: bin/test_parse_args
	$<


BUILT := $(strip $(foreach dir,$(BUILD_DIRS),$(wildcard $(dir)/*)))
ifneq ($(BUILT),)
	CLEAN := rm $(BUILT)
	# hack to force clean to run first *to completion* for parallel builds
	# note that $(info ...) prints everything on one line
	CLEAN_CROSS := _nop $(foreach _,$(filter clean,$(MAKECMDGOALS)),$(info $(shell $(MAKE) -s _clean)))
endif
clean: $(CLEAN_CROSS)
_clean:
	printf '%s\n' "$(CLEAN)"
	$(CLEAN)
_nop:
	@true
