CXX ?= g++
CXXFLAGS ?= -Werror -Wall -Wextra -Wold-style-cast -Wno-unused-parameter -O2 -std=c++17 -I. -I$(_OUTDIR)
WINDRES ?= LANG=C windres
LIBS ?= -lcomctl32 -lshell32 -luser32 -lgdi32
KEYHOOK_LIBS ?= -luser32

EXECUTION_LEVEL ?= highestAvailable
UI_ACCESS ?= false
SUBSYSTEM ?= windows

MSYSTEM ?= MINGW32
export MSYSTEM

ifeq ($(MSYSTEM),MINGW32)
TARGET_ARCH ?= X86
TARGET_BITS = 32bit
else
TARGET_ARCH ?= AMD64
TARGET_BITS = 64bit
ifndef FORCE_64BIT
$(error umapita should be compiled for 32bit target, because of keyhook)
endif
endif
OUTDIR ?= out
_OUTDIR ?= $(OUTDIR)

VER ?= $(shell git name-rev --tags --name-only --refs '20[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9]' HEAD | head -1)
VER_0 ?= 1
VER_1 ?= $(YEAR)
VER_2 ?= $(MONTHDAY)
VER_3 ?= $(REV)

_AMOUTDIR = $(_OUTDIR)/am
SRCS = $(wildcard am/*.cpp) umapita.cpp umapita_registry.cpp
OBJS = $(SRCS:%.cpp=$(_OUTDIR)/%.o)
DEPS = $(OBJS:$(_OUTDIR)/%.o=$(_OUTDIR)/%.d)
RC_SRCS = umapita_res.rc
RC_DEPENDS = umapita_res.h
RES = $(RC_SRCS:%.rc=$(_OUTDIR)/%.res)
MANIFEST_SRCS = umapita.manifest.tmpl
MANIFEST = $(MANIFEST_SRCS:%.manifest.tmpl=$(_OUTDIR)/%.manifest)
EXE = $(_OUTDIR)/umapita.exe
KEYHOOK_SRCS = umapita_keyhook.cpp
KEYHOOK_OBJS = $(KEYHOOK_SRCS:%.cpp=$(_OUTDIR)/%.o)
KEYHOOK_DEPS = $(KEYHOOK_OBJS:$(_OUTDIR)/%.o=$(_OUTDIR)/%.d)
KEYHOOK_DLL = $(_OUTDIR)/umapita_keyhook.dll
YEAR = $(shell echo $(VER) | sed -E 's/^(20[0-9][0-9]).*/\1/;s/undefined/0/')
MONTHDAY = $(shell echo $(VER) | sed -E 's/.*([0-9][0-9][0-9][0-9])-.*/\1/;s/^0//;s/undefined/0/')
REV = $(shell echo $(VER) | sed -E 's/.*-([0-9][0-9])$$/\1/;s/^0//;s/undefined/0/')
VERSTR = $(shell echo $(VER) | sed '/undefined/!s/^/ver./;s/undefined//')
WINDRES_VERDEF = -DVERSTR=\\\"$(VERSTR)\\\" -DVER_0=$(VER_0) -DVER_1=$(VER_1) -DVER_2=$(VER_2) -DVER_3=$(VER_3)

.PHONY: all clean debug release

all: $(EXE)

debug:
	@$(MAKE) --no-print-directory EXECUTION_LEVEL=asInvoker UI_ACCESS=false SUBSYSTEM=console OUTDIR=out.debug all

release:
	@$(MAKE) --no-print-directory _release OUTDIR=out.release VER=$(VER)

_RELEASE_ZIP = umapita-$(VER).zip
_release:
	@+test x$(VER) != x"undefined" || (echo error: VER is undefined. >&2; exit 1)
	@+test x$(VER) != x"" || (echo error: VER not defined. >&2; exit 1)
	rm -rf $(_OUTDIR)
	@$(MAKE) --no-print-directory OUTDIR=$(OUTDIR) all
	rm -f $(_RELEASE_ZIP)
	zip -j $(_RELEASE_ZIP) README.md $(EXE) $(KEYHOOK_DLL)

_dep: $(DEPS)

_keyhook_dep: $(KEYHOOK_DEPS)

-include $(DEPS) $(KEYHOOK_DEPS)

$(EXE): $(OBJS) $(RES) $(KEYHOOK_DLL) | _dep
	$(CXX) -static $(CXXFLAGS) -m$(SUBSYSTEM) -g -o $@ $(OBJS) $(RES) $(KEYHOOK_DLL) $(LIBS)

$(KEYHOOK_DLL): $(KEYHOOK_OBJS) | _keyhook_dep
	$(CXX) -static -shared $(CXXFLAGS) -g -o $@ $(KEYHOOK_OBJS) $(KEYHOOK_LIBS)

$(_OUTDIR)/pch.h.gch: pch.h am/pch.h | $(_OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(_AMOUTDIR)/pch.h.gch: am/pch.h | $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(_OUTDIR)/%.o: %.cpp $(_OUTDIR)/pch.h.gch | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(_OUTDIR)/%.d: %.cpp | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.o} $<

$(_OUTDIR):
	@test -e $(_OUTDIR) || mkdir $(_OUTDIR)

$(_AMOUTDIR): $(_OUTDIR)
	@test -e $(_AMOUTDIR) || mkdir $(_AMOUTDIR)

$(RES): $(RC_SRCS) $(RC_DEPENDS) $(MANIFEST) umapita.ico | $(_OUTDIR)
	$(WINDRES) -I$(_OUTDIR) $(WINDRES_VERDEF) --output-format=coff -o $@ $<

$(MANIFEST): $(MANIFEST_SRCS)
	sed	-e 's/@@@EXECUTION_LEVEL@@@/$(EXECUTION_LEVEL)/g' \
		-e 's/@@@UI_ACCESS@@@/$(UI_ACCESS)/g' \
		-e 's/@@@TARGET_ARCH@@@/$(TARGET_ARCH)/g' \
		$< > $@
