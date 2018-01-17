LEX 		= flex
BISON 		= bison
CC 			= g++
AM_CPPFLAGS	= -Wall -DGOSSIP_PUSHPULL -DMERIDIAN_DSL -DPLANET_LAB_SUPPORT -DAPPNAME="${APPNAME}"
AM_LDFLAGS 	= -lcblas -lgfortran -lqhull -lcurl -lresolv -lz -lcrypto -ldl
LD 			= g++

bindir = $(top_builddir)/bin
libdir = $(top_builddir)/lib
includedir = $(top_builddir)/include

BUILT_SOURCES = MQL.flex.cpp\
				MQL.tab.hpp\
				MQL.tab.cpp

EXTRA_DIST = MQL.lex\
			MQL.ypp\
			closestNode.b

MQL.flex.cpp: MQL.lex
	$(LEX) -o$@ $<

MQL.tab.hpp MQL.tab.cpp: MQL.ypp
	$(BISON) -d $<

include_HEADERS = meridian.h\
				Common.h\
				DSLLauncher.h\
				GramSchmidtOpt.h\
				LatencyCache.h\
				Marshal.h\
				MeridianDemo.h\
				MeridianDSL.h\
				MeridianProcess.h\
				MQLState.h\
				Query.h\
				QueryTable.h\
				RingSet.h

lib_LIBRARIES = libMeridian.a
libMeridian_a_SOURCES = GramSchmidtOpt.cpp\
						Query.cpp\
						QueryTable.cpp\
						RingSet.cpp\
						MeridianProcess.cpp\
						Marshal.cpp\
						LatencyCache.cpp\
						MQLState.cpp\
						MeridianDSL.cpp\
						meridian.cpp\
						MQL.flex.cpp\
						MQL.tab.cpp\
						odhtMeridian.cpp

bin_PROGRAMS = demoMQL\
				DSLLaunch\
				demoODHT\
				demoMeridian\
				demoClosest\
				demoPinger\
				demoMultiConst
				

demoMQL_SOURCES = DSLStandAlone.cpp
demoMQL_LDADD = $(top_builddir)/libMeridian.a
demoMQL_DEPENDENCIES = libMeridian.a

DSLLaunch_SOURCES = DSLLauncher.cpp\
					DSLLaunchCommon.cpp
DSLLaunch_LDADD = $(top_builddir)/libMeridian.a
DSLLaunch_DEPENDENCIES = libMeridian.a

demoODHT_SOURCES = odhtMeridian.cpp
demoODHT_LDADD = $(top_builddir)/libMeridian.a
demoODHT_DEPENDENCIES = libMeridian.a

demoMeridian_SOURCES = DemoMeridian.cpp
demoMeridian_LDADD = $(top_builddir)/libMeridian.a
demoMeridian_DEPENDENCIES = libMeridian.a

demoClosest_SOURCES = DemoClosestSearch.cpp
demoClosest_LDADD = $(top_builddir)/libMeridian.a
demoClosest_DEPENDENCIES = libMeridian.a

demoPinger_SOURCES = DemoPinger.cpp
demoPinger_LDADD = $(top_builddir)/libMeridian.a
demoPinger_DEPENDENCIES = libMeridian.a

demoMultiConst_SOURCES = DemoMultiConstraint.cpp
demoMultiConst_LDADD = $(top_builddir)/libMeridian.a
demoMultiConst_DEPENDENCIES = libMeridian.a


all: fail

fail: 
	if [ -z "${APPNAME}" ]; then \
		echo "Must specify APPNAME to build. For example: make APPNAME=\"meridian\"";\
		exit 1;\
	fi

.PHONY: fail

