sbin_PROGRAMS      = srs-daemon
bin_PROGRAMS       =

plugindir          = $(libdir)/src/plugins
plugin_LTLIBRARIES =

INCLUDES           = -I$(top_builddir)
AM_CFLAGS          = $(INCLUDES)

QUIET_GEN          = $(Q:@=@echo '  GEN   '$@;)

# SRS daemon
srs_daemon_PUBLIC_SYMBOLS =			\
		daemon/context.h		\
		daemon/audiobuf.h		\
		daemon/recognizer.h		\
		daemon/plugin.h

srs_daemon_SOURCES =				\
		daemon/daemon.c			\
		daemon/config.c			\
		daemon/resourceif.c		\
		daemon/client.c			\
		daemon/plugin.c			\
		daemon/audiobuf.c		\
		daemon/recognizer.c		\
		daemon/voice.c

srs_daemon_CFLAGS = 				\
		$(AM_CFLAGS)			\
		$(MURPHY_PULSE_CFLAGS)		\
		$(MURPHY_COMMON_CFLAGS)		\
		$(MURPHY_RESOURCE_CFLAGS)	\
		$(PULSE_CFLAGS)

srs_daemon_LDADD =				\
		$(MURPHY_PULSE_LIBS)		\
		$(MURPHY_RESOURCE_LIBS)		\
		$(MURPHY_COMMON_LIBS)		\
		$(PULSE_LIBS)			\
		-ldl

srs_daemon_LDFLAGS =				\
		-rdynamic

if DBUS_ENABLED
# SRS command line test clinet
bin_PROGRAMS += srs-client

srs_client_SOURCES =				\
		client/client.c

srs_client_CFLAGS =				\
		$(AM_CFLAGS)			\
		$(MURPHY_PULSE_CFLAGS)		\
		$(MURPHY_DBUS_CFLAGS)		\
		$(MURPHY_BREEDLINE_CFLAGS)	\
		$(MURPHY_COMMON_CFLAGS)		\
		$(DBUS_CFLAGS)

srs_client_LDADD =				\
		$(MURPHY_PULSE_LIBS)		\
		$(MURPHY_DBUS_LIBS)		\
		$(MURPHY_BREEDLINE_LIBS)	\
		$(MURPHY_COMMON_LIBS)		\
		$(PULSE_LIBS)			\
		$(DBUS_LIBS)
endif


if DBUS_ENABLED
# D-Bus client interface plugin
plugin_LTLIBRARIES += plugin-dbus-client.la

plugin_dbus_client_la_SOURCES = 		\
		plugins/dbus-client-interface/dbus-client.c

plugin_dbus_client_la_CFLAGS =			\
		$(AM_CFLAGS)			\
		$(DBUS_CFLAGS)			\
		$(MURPHY_DBUS_CFLAGS)

plugin_dbus_client_la_LDFLAGS =			\
		-module -avoid-version

plugin_dbus_client_la_LIBADD =			\
		$(MURPHY_DBUS_LIBS)
endif

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
if SPHINX_ENABLED
plugin_LTLIBRARIES += plugin-sphinx-speech.la

plugin_sphinx_speech_la_SOURCES =				\
		plugins/sphinx-speech-engine/sphinx-plugin.c	\
		plugins/sphinx-speech-engine/pulse-interface.c	\
		plugins/sphinx-speech-engine/input-buffer.c	\
		plugins/sphinx-speech-engine/filter-buffer.c	\
		plugins/sphinx-speech-engine/utterance.c	\
		plugins/sphinx-speech-engine/decoder-set.c	\
		plugins/sphinx-speech-engine/options.c

plugin_sphinx_speech_la_CFLAGS  =			\
		$(AM_CFLAGS) 				\
		$(PULSE_CFLAGS) 			\
		$(SPHINX_CFLAGS)

plugin_sphinx_speech_la_LDFLAGS =			\
		-module -avoid-version

plugin_sphinx_speech_la_LIBADD  =			\
		$(PULSE_LIBS)				\
		$(SPHINX_LIBS)
endif

# SRS Nuance speech engine plugin
plugin_LTLIBRARIES += plugin-nuance-speech.la

plugin_nuance_speech_la_SOURCES =			\
		plugins/nuance-speech-engine/nuance.c

plugin_nuance_speech_la_CFLAGS  =			\
		$(AM_CFLAGS)

plugin_nuance_speech_la_LDFLAGS =			\
		-module -avoid-version

plugin_nuance_speech_la_LIBADD  =

# SRS fake speech engine plugin
plugin_LTLIBRARIES += plugin-simple-disambiguator.la

plugin_simple_disambiguator_la_SOURCES =			\
		plugins/simple-disambiguator/disambiguator.c

plugin_simple_disambiguator_la_CFLAGS  =			\
		$(AM_CFLAGS)

plugin_simple_disambiguator_la_LDFLAGS =			\
		-module -avoid-version

plugin_simple_disambiguator_la_LIBADD  =

if DBUS_ENABLED
# Mpris2 client plugin
plugin_LTLIBRARIES += plugin-mpris2-client.la

plugin_mpris2_client_la_SOURCES =				\
		plugins/mpris2-client/mpris2-plugin.c		\
		plugins/mpris2-client/dbusif.c			\
		plugins/mpris2-client/clients.c

plugin_mpris2_client_la_CFLAGS  =				\
		$(AM_CFLAGS)					\
		$(MURPHY_DBUS_CFLAGS)				\
		$(DBUS_CFLAGS)

plugin_mpris2_client_la_LDFLAGS =				\
		-module -avoid-version

plugin_mpris2_client_la_LIBADD  =				\
		$(MURPHY_DBUS_LIBS)				\
		$(DBUS_LIBS)
endif


if DBUS_ENABLED
# Bluetooth client plugin
plugin_LTLIBRARIES += plugin-bluetooth-client.la

plugin_bluetooth_client_la_SOURCES =				\
		plugins/bluetooth-client/bluetooth-plugin.c	\
		plugins/bluetooth-client/dbusif.c		\
		plugins/bluetooth-client/pulseif.c		\
		plugins/bluetooth-client/clients.c

plugin_bluetooth_client_la_CFLAGS  =				\
		$(AM_CFLAGS)					\
		$(MURPHY_DBUS_CFLAGS)				\
		$(DBUS_CFLAGS)

plugin_bluetooth_client_la_LDFLAGS =				\
		-module -avoid-version

plugin_bluetooth_client_la_LIBADD  =				\
		$(MURPHY_DBUS_LIBS)				\
		$(DBUS_LIBS)

endif

# search plugin
plugin_LTLIBRARIES += plugin-search-client.la

plugin_search_client_la_SOURCES =				\
		plugins/search-client/search-plugin.c

plugin_search_client_la_CFLAGS  =				\
		$(AM_CFLAGS)

plugin_search_client_la_LDFLAGS =				\
		-module -avoid-version

plugin_search_client_la_LIBADD  =

# simple-voice synthesizer plugin
plugin_LTLIBRARIES += plugin-simple-voice.la

plugin_simple_voice_la_SOURCES =				\
		plugins/simple-voice/simple-voice.c

plugin_simple_voice_la_CFLAGS  =				\
		$(AM_CFLAGS)

plugin_simple_voice_la_LDFLAGS =				\
		-module -avoid-version

plugin_simple_voice_la_LIBADD  =

# input plugin
plugin_LTLIBRARIES += plugin-input-handler.la

plugin_input_handler_la_SOURCES =				\
		plugins/input-handler/input-plugin.c

plugin_input_handler_la_CFLAGS  =				\
		$(AM_CFLAGS)

plugin_input_handler_la_LDFLAGS =				\
		$(UDEV_CFLAGS)					\
		-module -avoid-version

plugin_input_handler_la_LIBADD  =				\
		$(UDEV_LIBS)

# cleanup
clean-local::
	-rm -f *~
