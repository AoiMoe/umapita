AM_TOP := deps/amwin32
-include deps/amwin32/am.gmk

CXX ?= g++
CXXFLAGS ?= -Werror -Wall -Wextra -Wold-style-cast -Wno-unused-parameter -O2 -std=c++17 $(AM_CXXFLAGS) -I$(_OUTDIR)
WINDRES ?= LANG=C windres
LIBS ?= -lcomctl32 -lshell32 -luser32 -lgdi32

EXECUTION_LEVEL ?= highestAvailable
UI_ACCESS ?= false
SUBSYSTEM ?= windows

MSYSTEM ?= MINGW32
export MSYSTEM

ifeq ($(MSYSTEM),MINGW32)
TARGET_ARCH ?= X86
else ifeq ($(MSYSTEM),MINGW64)
TARGET_ARCH ?= AMD64
else ifeq ($(MSYSTEM),UCRT64)
TARGET_ARCH ?= AMD64
else
$(error unknown MSYSTEM variant: $(MSYSTEM))
endif

ifeq ($(TARGET_ARCH),X86)
TARGET_BITS = 32bit
TARGET_TRIPLET = i686-w64-mingw32
else ifeq ($(TARGET_ARCH),AMD64)
TARGET_BITS = 64bit
TARGET_TRIPLET = x86_64-w64-mingw32
endif

ifneq ($(shell gcc -dumpmachine),$(TARGET_TRIPLET))
$(error gcc does not support $(TARGET_TRIPLET))
endif

OUTDIR ?= out
_OUTDIR ?= $(OUTDIR)
ARCHIVE_DIR ?= archive

VER ?= $(shell git name-rev --tags --name-only --refs '20[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9]' HEAD | head -1)
VER_0 ?= 1
VER_1 ?= $(YEAR)
VER_2 ?= $(MONTHDAY)
VER_3 ?= $(REV)

_AMOUTDIR = $(_OUTDIR)/am
SRCS = umapita.cpp umapita_registry.cpp umapita_save_dialog_box.cpp umapita_target_status.cpp
OBJS = $(AM_SRCS:%.cpp=$(_OUTDIR)/%.o) $(SRCS:%.cpp=$(_OUTDIR)/%.o)
DEPS = $(_AMOUTDIR)/pch.h.d $(_OUTDIR)/pch.h.d $(AM_SRCS:%.cpp=$(_OUTDIR)/%.d) $(OBJS:$(_OUTDIR)/%.o=$(_OUTDIR)/%.d)
RC_SRCS = umapita_res.rc
RC_DEPENDS = umapita_res.h
RES = $(RC_SRCS:%.rc=$(_OUTDIR)/%.res)
MANIFEST_SRCS = umapita.manifest.tmpl
MANIFEST = $(MANIFEST_SRCS:%.manifest.tmpl=$(_OUTDIR)/%.manifest)
EXE = $(_OUTDIR)/umapita.exe
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
	@$(MAKE) --no-print-directory _release OUTDIR=out.release VER=$(VER) ARCHIVE_DIR=$(ARCHIVE_DIR)

_RELEASE_ZIP = $(ARCHIVE_DIR)/umapita-$(VER).zip
_release: $(ARCHIVE_DIR)
	@+test x$(VER) != x"undefined" || (echo error: VER is undefined. >&2; exit 1)
	@+test x$(VER) != x"" || (echo error: VER not defined. >&2; exit 1)
	rm -rf $(_OUTDIR)
	@$(MAKE) --no-print-directory OUTDIR=$(OUTDIR) all
	rm -f $(_RELEASE_ZIP)
	zip -j $(_RELEASE_ZIP) README.md LICENSE $(EXE)

_dep: $(DEPS)

-include $(DEPS)

$(EXE): $(OBJS) $(RES) | _dep
	$(CXX) -static $(CXXFLAGS) -m$(SUBSYSTEM) -g -o $@ $(OBJS) $(RES) $(LIBS)

$(_OUTDIR)/pch.h.gch: pch.h $(AM_TOP)/am/pch.h | $(_OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(_AMOUTDIR)/pch.h.gch: $(AM_TOP)/am/pch.h | $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(_OUTDIR)/%.o: %.cpp $(_OUTDIR)/pch.h.gch | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(_OUTDIR)/%.d: %.cpp | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.o} $<

$(_OUTDIR)/%.o: $(AM_TOP)/%.cpp $(_OUTDIR)/pch.h.gch | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(_OUTDIR)/%.d: $(AM_TOP)/%.cpp | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.o} $<

$(_OUTDIR)/pch.h.d: pch.h | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.gch} $<

$(_AMOUTDIR)/pch.h.d: $(AM_TOP)/am/pch.h | $(_OUTDIR) $(_AMOUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.gch} $<

$(_OUTDIR):
	@test -e $(_OUTDIR) || mkdir $(_OUTDIR)

$(_AMOUTDIR): $(_OUTDIR)
	@test -e $(_AMOUTDIR) || mkdir $(_AMOUTDIR)

$(ARCHIVE_DIR):
	@test -e $(ARCHIVE_DIR) || mkdir $(ARCHIVE_DIR)

$(RES): $(RC_SRCS) $(RC_DEPENDS) $(MANIFEST) umapita.ico | $(_OUTDIR)
	$(WINDRES) -I$(_OUTDIR) $(WINDRES_VERDEF) --output-format=coff -o $@ $<

$(MANIFEST): $(MANIFEST_SRCS)
	sed	-e 's/@@@EXECUTION_LEVEL@@@/$(EXECUTION_LEVEL)/g' \
		-e 's/@@@UI_ACCESS@@@/$(UI_ACCESS)/g' \
		-e 's/@@@TARGET_ARCH@@@/$(TARGET_ARCH)/g' \
		$< > $@
