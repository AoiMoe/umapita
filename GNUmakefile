OUTDIR ?= out
CXX ?= g++
CXXFLAGS ?= -Werror -Wall -Wextra -Wold-style-cast -Wno-unused-parameter -O2 -std=c++17 -I. -I$(OUTDIR)
WINDRES ?= LANG=C windres
LIBS ?= -lcomctl32 -lshell32 -luser32 -lgdi32

EXECUTION_LEVEL ?= highestAvailable
UI_ACCESS ?= false
SUBSYSTEM ?= windows
PROCESSOR_ARCHITECTURE ?= AMD64

MSYSTEM ?= MINGW64
export MSYSTEM

VER ?= $(shell git name-rev --tags --name-only --refs '20[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9]' HEAD | head -1)
VER_0 ?= 1
VER_1 ?= $(YEAR)
VER_2 ?= $(MONTHDAY)
VER_3 ?= $(REV)

AMOUTDIR = $(OUTDIR)/am
SRCS = $(wildcard am/*.cpp) $(wildcard *.cpp)
OBJS = $(SRCS:%.cpp=$(OUTDIR)/%.o)
DEPS = $(OBJS:$(OUTDIR)/%.o=$(OUTDIR)/%.d)
RC_SRCS = umapita_res.rc
RC_DEPENDS = umapita_res.h
RES = $(RC_SRCS:%.rc=$(OUTDIR)/%.res)
MANIFEST_SRCS = umapita.manifest.tmpl
MANIFEST = $(MANIFEST_SRCS:%.manifest.tmpl=$(OUTDIR)/%.manifest)
EXE = $(OUTDIR)/umapita.exe
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
	@test x$(VER) != x"undefined" || (echo error: VER is undefined. >&2; exit 1)
	@test x$(VER) != x"" || (echo error: VER not defined. >&2; exit 1)
	rm -rf $(OUTDIR)
	@$(MAKE) --no-print-directory OUTDIR=$(OUTDIR) all
	rm -f $(_RELEASE_ZIP)
	zip -j $(_RELEASE_ZIP) README.md $(EXE)

_dep: $(DEPS)

-include $(DEPS)

$(EXE): $(OBJS) $(RES) | _dep
	$(CXX) -static $(CXXFLAGS) -m$(SUBSYSTEM) -g -o $@ $(OBJS) $(RES) $(LIBS)

$(OUTDIR)/pch.h.gch: pch.h am/pch.h | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(AMOUTDIR)/pch.h.gch: am/pch.h | $(AMOUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(OUTDIR)/%.o: %.cpp $(OUTDIR)/pch.h.gch | $(OUTDIR) $(AMOUTDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUTDIR)/%.d: %.cpp | $(OUTDIR) $(AMOUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.o} $<

$(OUTDIR):
	@test -e $(OUTDIR) || mkdir $(OUTDIR)

$(AMOUTDIR): $(OUTDIR)
	@test -e $(AMOUTDIR) || mkdir $(AMOUTDIR)

$(RES): $(RC_SRCS) $(RC_DEPENDS) $(MANIFEST) umapita.ico | $(OUTDIR)
	$(WINDRES) -I$(OUTDIR) $(WINDRES_VERDEF) --output-format=coff -o $@ $<

$(MANIFEST): $(MANIFEST_SRCS)
	sed	-e 's/@@@EXECUTION_LEVEL@@@/$(EXECUTION_LEVEL)/g' \
		-e 's/@@@UI_ACCESS@@@/$(UI_ACCESS)/g' \
		-e 's/@@@PROCESSOR_ARCHITECTURE@@@/$(PROCESSOR_ARCHITECTURE)/g' \
		$< > $@
