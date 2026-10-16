.SUFFIXES: .cc .c .o .h .d

ifdef DEBUG
OPT=-g
else
OPT=-O3 -DNDEBUG
endif

FLAGS+=-Isrc -I$(HOME)/usr/include -I/usr/local/include -Werror 
PKG_CONFIG=pkg-config

FLAGS+=$(shell $(PKG_CONFIG) --cflags cairo)

OS=$(shell uname)

ifdef MXE
MXE_32bit=$(shell if which i686-w64-mingw32.static-g++>&/dev/null; then echo 1; fi)

MXE_64bit_static=$(shell if which x86_64-w64-mingw32.static-g++>&/dev/null; then echo 1; fi)
MXE_64bit_shared=$(shell if which x86_64-w64-mingw32.shared-g++>&/dev/null; then echo 1; fi)

  ifeq ($(MXE_32bit),1)
    MXE_PREFIX=i686-w64-mingw32.static
  else
    ifeq ($(MXE_64bit_static),1)  # prefer static to shared
      MXE_PREFIX=x86_64-w64-mingw32.static
    else
      ifeq ($(MXE_64bit_shared),1)
        MXE_PREFIX=x86_64-w64-mingw32.shared
      else
        $(error "MXE compiler not found")
      endif
    endif
  endif

GCC=1
FLAGS+=-DSTATIC_BUILD -DNO_FWD_DECLARE_STL -DMXE
CC=$(MXE_PREFIX)-gcc
CPLUSPLUS=$(MXE_PREFIX)-g++
PKG_CONFIG=$(MXE_PREFIX)-pkg-config
LIBS+=-lgdiplus

# xdr on MXE is positively ancient, so just disable it here
XDR=

# rule for running the resource compiler
.rc.o:
	$(MXE_PREFIX)-windres -O coff -i $< -o $@

.rc.d:
	touch $@
else
CPLUSPLUS=g++
CC=gcc

LIBS+=-L/opt/local/lib
FLAGS+=-fPIC -I
endif

# add standard list of search paths, -L/usr/lib32 must be before this statement
LIBS+= $(DIRS:%=-L%/lib) 
FLAGS+=$(DIRS:%=-I%/include)

LINK=$(CPLUSPLUS)
CPP=$(CPLUSPLUS) -E
CXXFLAGS+=-std=c++20

# cairo dependencies
FLAGS+=-DCAIRO $(shell $(PKG_CONFIG) --cflags cairo)
LIBS+=$(shell $(PKG_CONFIG) --libs cairo)
# RSVG dependencies calculated here
FLAGS+=$(shell $(PKG_CONFIG) --cflags librsvg-2.0)
LIBS+=$(shell $(PKG_CONFIG) --libs librsvg-2.0)
# pango dependencies
FLAGS+=-DPANGO $(shell $(PKG_CONFIG) --cflags pangocairo)
LIBS+=$(shell $(PKG_CONFIG) --libs pangocairo)
LIBS+=$(shell $(PKG_CONFIG) --libs gobject-2.0)


# assorted performance profiling stuff using gperftools, or Russell's custom
# timer calipers
ifdef MEMPROFILE
LIBS+=-ltcmalloc
endif
ifdef CPUPROFILE
OPT+=-g
LIBS+=-lprofiler
endif
ifdef TIMERS
FLAGS+=-DTIMERS
endif
ifdef GCOV
FLAGS+=-fprofile-arcs -ftest-coverage
endif

.c.d: 
	$(CC) $(FLAGS) -MM -MG  $< >$@

.cc.d: 
	$(CPLUSPLUS) $(FLAGS)  $(CXXFLAGS) -MM -MG $< >$@

.cc.o: 
	$(CPLUSPLUS) -c $(FLAGS) $(CXXFLAGS) $(OPT) -o $@ $<

.c.o: 
	$(CC) -c $(FLAGS) $(OPT) -o $@ $<

VPATH+=src 
FLAGS+=-I. -Isrc 

# object files making up libmansoura
OBJS=mansouraCairo.o plot.o SVGItem.o
LIBS:=-L$(HOME)/usr/lib64 $(LIBS)

DLLS=

# need to append this as late as possible, to override injection of -Werror on some distros.
ifdef NOWERROR
FLAGS+=-Wno-error
endif

ifdef MXE
  DL=dll
  PYRAVEL=pyravel.pyd
  PYTHONCAPI=pythonCAPI.o # extra python.lib shims required on Windows
  FLAGS+=-D_WIN32 -I$(HOME)/usr/mxe/include -L$(HOME)/usr/mxe/lib
  LIBS+=-lsoci_core_4_0 -lsoci_sqlite3_4_0 -lsoci_odbc_4_0 -lsoci_postgresql_4_0
else
  DL=so
  # TODO - may enable python on MacOSX?
  ifeq ($(OS),Darwin)
    PYRAVEL=
  else
    PYRAVEL=pyravel.so
  endif
  LIBS+=-lsoci_core
endif

ifdef AEGIS
aegis-all: all
	cd test; $(MAKE)
endif

#chmod command is to counteract AEGIS removing execute privelege from scripts
#Ravel/Installer/ravelDoc.wxi dependency removed
all: $(MODELS) $(EXES)  libmansoura.$(DL) 

ifeq ($(OS),Darwin)
all: $(MODELS:=.app)
endif

# we want to build this target always when under AEGIS, otherwise only
# when non-existing
ifdef AEGIS
.PHONY: src/ravelVersion.h
endif

# This rule uses a header file of object descriptors
$(MODELS:=.o): %.o: %.cc 
#	$(CPLUSPLUS) -c $(FLAGS)  $<

# how to build a model executable
$(MODELS): %: %.o libmansoura.a
	$(LINK) $(FLAGS) $*.o $(MODLINK) $(LIBS) -o $@

$(EXES): %: %.o libmansoura.a 
	$(LINK) $(FLAGS) $*.o $(LIBS) -o $@

libmansoura.a: $(OBJS)
	ar r $@ $^

libmansoura.$(DL): $(OBJS)
	$(LINK) $(FLAGS) -shared $^ $(LIBS) -o $@
ifeq ($(OS),Darwin)
	otool -L $@|grep opt/|cut -f1 -d' '|while read dylib; do install_name_tool -change $$dylib @executable_path/../Resources/build/$${dylib##*/} $@; done
endif

test:
	otool -L $@|grep opt/|cut -f1 -d' '|while read dylib; do install_name_tool -change $$dylib @loader_path/$${dylib##*/} libravel.so; done


ifneq ($(MAKECMDGOALS),clean)
include $(MODELS:=.d) 
include $(OBJS:.o=.d)
endif

BASIC_CLEAN=rm -f *.o  *~ \\\#* core *.exh *.exc *.d *,D *.exe *.cd *.gc* *.so
clean:
	$(BASIC_CLEAN) libravel.* pyravel.so pyravel.pyd RavelCAPI/build.log RavelCAPI/classdesc/build.log $(LICENSES)
	cd src && $(BASIC_CLEAN)
	#cd test && $(BASIC_CLEAN)

tests: all
	cd test; $(MAKE)

sure: tests
	sh runTests.sh

install: libravel.$(DL)
	mkdir -p $(LIBPREFIX)
	cp libravel.$(DL) $(LIBPREFIX)

install-web: doc/javascriptAPI/index.html doc/ravelDoc/index.html
	$(MAKE) -C javascript-widget
	rsync -z -P -r mySqlService.php doc/javascriptAPI doc/ravelDoc hpcoders:$(WEBINSTALLROOT)
	rsync -z -P -r javascript-widget/*.html javascript-widget/*.js javascript-widget/*.wasm hpcoders:$(WEBINSTALLEXAMPLES)

clang-check:
	$(MAKE) CPLUSPLUS="/usr/bin/clang++ -I/usr/lib64/clang/4.0.1/include"

lcov:
	$(MAKE) clean
	-$(MAKE) GCOV=1 tests
	lcov --keep-going --i -c -d . --no-external -o lcovi.info
	-$(MAKE) GCOV=1 sure
	lcov --keep-going -c -d . --no-external -o lcovt.info
	lcov --keep-going -a lcovi.info -a lcovt.info -o lcov.info
	lcov --keep-going -r lcov.info "*.cd" -o lcovr.info 
	genhtml -o coverage lcovr.info

codeql:
	rm *.o
	codeql database create codeqlDb -l c++ -c "$(MAKE)" -j0 --overwrite
	codeql database analyze codeqlDb codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls --format=csv --output=codeql-c++.csv -j0
