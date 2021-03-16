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

.PHONY: all clean debug release

all: $(EXE)

debug:
	@$(MAKE) --no-print-directory EXECUTION_LEVEL=asInvoker UI_ACCESS=false SUBSYSTEM=console OUTDIR=out.debug all

release:
	@$(MAKE) --no-print-directory _release OUTDIR=out.release

_RELEASE_ZIP = umapita-$(VER).zip
_release:
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

$(RES): $(RC_SRCS) $(RC_DEPENDS) $(MANIFEST) | $(OUTDIR)
	$(WINDRES) -I$(OUTDIR) --output-format=coff -o $@ $<

$(MANIFEST): $(MANIFEST_SRCS)
	sed	-e 's/@@@EXECUTION_LEVEL@@@/$(EXECUTION_LEVEL)/g' \
		-e 's/@@@UI_ACCESS@@@/$(UI_ACCESS)/g' \
		-e 's/@@@PROCESSOR_ARCHITECTURE@@@/$(PROCESSOR_ARCHITECTURE)/g' \
		$< > $@
