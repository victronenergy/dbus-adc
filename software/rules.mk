DBUS = 1
#NMEA2K = 1

#ifdef DBUS
T = dbus-adc$(EXT)
#else
#T = foo$(EXT)
#endif

TARGETS += $T
INSTALL_BIN += $T

SUBDIRS += ext/velib
$T_DEPS += $(call subtree_tgts,$(d)/ext/velib)

SUBDIRS += ext/veutil
$T_DEPS += $(call subtree_tgts,$(d)/ext/veutil)

SUBDIRS += src
$T_DEPS += $(call subtree_tgts,$(d)/src)

#ifdef DBUS
DEFINES += DBUS
override CFLAGS += $(shell pkg-config --cflags dbus-1)
$T_LIBS += -lpthread -ldl `pkg-config --libs dbus-1` -levent -levent_pthreads
#endif

ifdef POSIX
$T_LIBS += -lpthread -ldl -lm
endif

ifdef WINDOWS
CFLAGS += -DUNICODE -D_UNICODE
$T_LIBS += -lwinmm -lShlwapi
endif

INCLUDES += inc
INCLUDES += ext/velib/inc
