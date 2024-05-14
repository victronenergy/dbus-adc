T = dbus-adc$(EXT)

TARGETS += $T
INSTALL_BIN += $T

SUBDIRS += ext/velib
$T_DEPS += $(call subtree_tgts,$(d)/ext/velib)

SUBDIRS += src
$T_DEPS += $(call subtree_tgts,$(d)/src)

DEFINES += DBUS
DBUS_CFLAGS += $(shell pkg-config --cflags dbus-1)
DBUS_LIBS += $(shell pkg-config --libs dbus-1)

override CFLAGS += $(DBUS_CFLAGS) -Werror
$T_LIBS += -lm -lpthread -levent -levent_pthreads $(DBUS_LIBS)

INCLUDES += inc
INCLUDES += ext/velib/inc
