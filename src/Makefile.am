sbin_PROGRAMS      = srs-daemon
bin_PROGRAMS       = srs-client

plugindir          = $(libdir)/src/plugins
plugin_LTLIBRARIES =

INCLUDES           = -I$(top_builddir)
AM_CFLAGS          = $(INCLUDES) $(DBUS_CFLAGS)

QUIET_GEN          = $(Q:@=@echo '  GEN   '$@;)

# SRS daemon
srs_daemon_PUBLIC_SYMBOLS =			\
		daemon/context.h		\
		daemon/recognizer.h		\
		daemon/plugin.h

srs_daemon_SOURCES =				\
		daemon/daemon.c			\
		daemon/config.c			\
		daemon/dbusif.c			\
		daemon/resourceif.c		\
		daemon/client.c			\
		daemon/plugin.c			\
		daemon/recognizer.c		\
		daemon/router.c

srs_daemon_CFLAGS = 				\
		$(AM_CFLAGS)			\
		$(PULSE_CFLAGS)			\
		$(DBUS_CFLAGS)

srs_daemon_LDADD =				\
		../src/libmurphy-pulse.la	\
		../src/libmurphy-dbus.la	\
		../src/libmurphy-common.la	\
		../src/libmurphy-resource.la	\
		$(PULSE_LIBS)			\
		$(DBUS_LIBS)			\
		-ldl

srs_daemon_LDFLAGS =				\
		-rdynamic

# SRS command line test clinet
srs_client_SOURCES =				\
		client/client.c

srs_client_CFLAGS =				\
		$(AM_CFLAGS)			\
		$(DBUS_CFLAGS)

srs_client_LDADD =				\
		../src/libmurphy-pulse.la	\
		../src/libmurphy-dbus.la	\
		../src/libbreedline-murphy.la	\
		../src/libmurphy-common.la	\
		../src/libbreedline.la		\
		$(PULSE_LIBS)			\
		$(DBUS_LIBS)

# SRS fake speech engine plugin
plugin_LTLIBRARIES += plugin-fake-speech.la

plugin_fake_speech_la_SOURCES =			\
		plugins/fake-speech-engine/fake.c

plugin_fake_speech_la_CFLAGS  =			\
		$(AM_CFLAGS)

plugin_fake_speech_la_LDFLAGS =			\
		-module -avoid-version

plugin_fake_speech_la_LIBADD  =

# SRS CMU Sphinx speech engine plugin
plugin_LTLIBRARIES += plugin-sphinx-speech.la

plugin_sphinx_speech_la_SOURCES =			\
		plugins/sphinx-speech-engine/sphinx.c

plugin_sphinx_speech_la_CFLAGS  =			\
		$(AM_CFLAGS)

plugin_sphinx_speech_la_LDFLAGS =			\
		-module -avoid-version

plugin_sphinx_speech_la_LIBADD  =

# SRS Nuance speech engine plugin
plugin_LTLIBRARIES += plugin-nuance-speech.la

plugin_nuance_speech_la_SOURCES =			\
		plugins/nuance-speech-engine/nuance.c

plugin_nuance_speech_la_CFLAGS  =			\
		$(AM_CFLAGS)

plugin_nuance_speech_la_LDFLAGS =			\
		-module -avoid-version

plugin_nuance_speech_la_LIBADD  =

# cleanup
clean-local::
	-rm -f *~
