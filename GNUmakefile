OUTDIR ?= out
CXX ?= g++
CXXFLAGS ?= -Werror -Wall -Wextra -Wold-style-cast -Wno-unused-parameter -O2 -std=c++17 -I. -I$(OUTDIR)
WINDRES ?= LANG=C windres
LIBS ?= -lshell32 -luser32 -lgdi32
EXECUTION_LEVEL ?= highestAvailable
UI_ACCESS ?= false
SUBSYSTEM ?= windows

MSYSTEM ?= MINGW64
export MSYSTEM

SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:%.cpp=$(OUTDIR)/%.o)
DEPS = $(OBJS:$(OUTDIR)/%.o=$(OUTDIR)/%.d)
RC_SRCS = umapita_res.rc
RC_DEPENDS = umapita_res.h
RES = $(RC_SRCS:%.rc=$(OUTDIR)/%.res)
MANIFEST_SRCS = umapita.manifest.tmpl
MANIFEST = $(MANIFEST_SRCS:%.manifest.tmpl=$(OUTDIR)/%.manifest)

.PHONY: all clean

all: $(OUTDIR)/umapita.exe

_dep: $(DEPS)

-include $(DEPS)

$(OUTDIR)/umapita.exe: $(OBJS) $(RES) | _dep
	$(CXX) -static $(CXXFLAGS) -m$(SUBSYSTEM) -g -o $@ $< $(RES) $(LIBS)

$(OUTDIR)/pch.h.gch: pch.h | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(OUTDIR)/%.o: %.cpp $(OUTDIR)/pch.h.gch | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUTDIR)/%.d: %.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -MM -MF $@ -MT ${@:.d=.o} $<

$(OUTDIR):
	test -e $(OUTDIR) || mkdir $(OUTDIR)

$(RES): $(RC_SRCS) $(RC_DEPENDS) $(MANIFEST) | $(OUTDIR)
	$(WINDRES) -I$(OUTDIR) --output-format=coff -o $@ $<

$(MANIFEST): $(MANIFEST_SRCS)
	sed 's/@@@EXECUTION_LEVEL@@@/$(EXECUTION_LEVEL)/;s/@@@UI_ACCESS@@@/$(UI_ACCESS)/' $< > $@
