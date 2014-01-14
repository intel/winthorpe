sbin_PROGRAMS      = srs-daemon
bin_PROGRAMS       =

plugindir          = $(libdir)/srs/plugins
plugin_LTLIBRARIES =

lib_LTLIBRARIES    =
EXTRA_DIST         =
pkgconfig_DATA     =
pkgconfigdir       = ${libdir}/pkgconfig

AM_CPPFLAGS        = -I$(top_builddir)
AM_CFLAGS          = $(GLIB_CFLAGS)

QUIET_GEN          = $(Q:@=@echo '  GEN   '$@;)

# SRS daemon
daemon_includedir  = $(includedir)/srs/daemon
daemon_include_HEADERS = \
		daemon/client-api-types.h	\
		daemon/voice-api-types.h

srs_daemon_SOURCES =				\
		daemon/daemon.c			\
		daemon/config.c			\
		daemon/resctl.c			\
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
		$(PULSE_CFLAGS)			\
		$(PULSE_GLIB_CFLAGS)		\
		$(MURPHY_GLIB_CFLAGS)		\
		$(GLIB_CFLAGS)			\
		$(SYSTEMD_CFLAGS)

srs_daemon_LDADD =				\
		$(MURPHY_PULSE_LIBS)		\
		$(MURPHY_RESOURCE_LIBS)		\
		$(MURPHY_COMMON_LIBS)		\
		$(PULSE_LIBS)			\
		$(PULSE_GLIB_LIBS)		\
		$(MURPHY_GLIB_LIBS)		\
		$(GLIB_LIBS)			\
		$(SYSTEMD_LIBS)			\
		-ldl

srs_daemon_LDFLAGS =				\
		-rdynamic

if DBUS_ENABLED
# D-Bus client API plugin
plugin_LTLIBRARIES += plugin-dbus-client.la

plugin_dbus_client_la_SOURCES = 		\
		plugins/client-api/dbus/dbus-client.c

plugin_dbus_client_la_CFLAGS =			\
		$(AM_CFLAGS)			\
		$(DBUS_CFLAGS)			\
		$(MURPHY_DBUS_CFLAGS)

plugin_dbus_client_la_LDFLAGS =			\
		-module -avoid-version

plugin_dbus_client_la_LIBADD =			\
		$(MURPHY_DBUS_LIBS)

# D-Bus client API command line test client
bin_PROGRAMS += srs-dbus-client

srs_dbus_client_SOURCES =				\
		plugins/client-api/dbus/test-client.c

srs_dbus_client_CFLAGS =			\
		$(AM_CFLAGS)			\
		$(MURPHY_PULSE_CFLAGS)		\
		$(MURPHY_DBUS_CFLAGS)		\
		$(MURPHY_BREEDLINE_CFLAGS)	\
		$(MURPHY_COMMON_CFLAGS)		\
		$(DBUS_CFLAGS)

srs_dbus_client_LDADD =				\
		$(MURPHY_PULSE_LIBS)		\
		$(MURPHY_DBUS_LIBS)		\
		$(MURPHY_BREEDLINE_LIBS)	\
		$(MURPHY_COMMON_LIBS)		\
		$(PULSE_LIBS)			\
		$(DBUS_LIBS)
endif


# native client API plugin
plugin_LTLIBRARIES += plugin-native-client.la

plugin_native_client_la_SOURCES = 					\
		plugins/client-api/native/native-server.c		\
		plugins/client-api/native/native-messages.c

plugin_native_client_la_CFLAGS =		\
		$(AM_CFLAGS)			\
		$(MURPHY_COMMON_CFLAGS)

plugin_native_client_la_LDFLAGS =		\
		-module -avoid-version

plugin_native_client_la_LIBADD =		\
		$(MURPHY_COMMON_LIBS)

# native client API library
lib_LTLIBRARIES += libsrs-native-client.la
EXTRA_DIST      += plugins/client-api/native/srs-native-client.pc
pkgconfig_DATA  += plugins/client-api/native/srs-native-client.pc

libsrs_native_client_ladir      = $(includedir)/srs/client-api/native
libsrs_native_client_la_HEADERS =	\
		plugins/client-api/native/native-client.h \
		plugins/client-api/native/native-config.h \
		plugins/client-api/native/native-messages.h

libsrs_native_client_la_SOURCES =	\
		plugins/client-api/native/native-client.c \
		plugins/client-api/native/native-messages.c

libsrs_native_client_la_CFLAGS =	\
		$(AM_CFLAGS)	\
		$(MURPHY_COMMON_CFLAGS)

libsrs_native_client_la_LIBADD =	\
		$(MURPHY_COMMON_LIBS)

# native client API command line test client
bin_PROGRAMS += srs-native-client

srs_native_client_SOURCES =						\
		plugins/client-api/native/test-client.c

srs_native_client_CFLAGS =			\
		$(AM_CFLAGS)			\
		$(MURPHY_PULSE_CFLAGS)		\
		$(MURPHY_BREEDLINE_CFLAGS)	\
		$(MURPHY_COMMON_CFLAGS)		\
		$(MURPHY_GLIB_FLAGS)

srs_native_client_LDADD =			\
		$(MURPHY_PULSE_LIBS)		\
		$(MURPHY_BREEDLINE_LIBS)	\
		$(MURPHY_COMMON_LIBS)		\
		$(MURPHY_GLIB_LIBS)		\
		$(PULSE_LIBS)			\
		libsrs-native-client.la

libsrs_native_client_la_LDFLAGS =	\
		-version-info @SRS_VERSION_INFO@

# SRS fake speech engine plugin
plugin_LTLIBRARIES += plugin-fake-speech.la

plugin_fake_speech_la_SOURCES =			\
		plugins/speech-to-text/fake/fake.c

plugin_fake_speech_la_CFLAGS  =			\
		$(AM_CFLAGS)

plugin_fake_speech_la_LDFLAGS =			\
		-module -avoid-version

plugin_fake_speech_la_LIBADD  =

# SRS CMU Sphinx speech engine plugin
if SPHINX_ENABLED
plugin_LTLIBRARIES += plugin-sphinx-speech.la

plugin_sphinx_speech_la_SOURCES =				\
		plugins/speech-to-text/sphinx/sphinx-plugin.c   \
		plugins/speech-to-text/sphinx/pulse-interface.c \
		plugins/speech-to-text/sphinx/input-buffer.c    \
		plugins/speech-to-text/sphinx/filter-buffer.c   \
		plugins/speech-to-text/sphinx/utterance.c	      \
		plugins/speech-to-text/sphinx/decoder-set.c     \
		plugins/speech-to-text/sphinx/options.c

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
		plugins/speech-to-text/nuance/nuance.c

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

if MPRIS2_ENABLED
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


if BLUETOOTH_ENABLED
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

if FESTIVAL_ENABLED
# festival-loader plugin
plugin_LTLIBRARIES += plugin-festival-loader.la

plugin_festival_loader_la_SOURCES =				\
		plugins/text-to-speech/festival/festival-loader.c

plugin_festival_loader_la_CFLAGS  =				\
		$(AM_CFLAGS)

plugin_festival_loader_la_LDFLAGS =				\
		-module -avoid-version

plugin_festival_loader_la_LIBADD = -ldl

# festival-voice synthesizer plugin
plugin_LTLIBRARIES += plugin-festival-voice.la

plugin_festival_voice_la_SOURCES =					\
		plugins/text-to-speech/festival/festival-voice.c	\
		plugins/text-to-speech/festival/libcarnival.cc		\
		plugins/text-to-speech/festival/pulse.c

plugin_festival_voice_la_CFLAGS  =				\
		$(AM_CFLAGS)

plugin_festival_voice_la_CXXFLAGS =				\
		$(AM_CFLAGS)					\
		$(AM_CXXFLAGS)					\
		$(FESTIVAL_CXXFLAGS)

plugin_festival_voice_la_LDFLAGS =				\
		-module -avoid-version
endif

# simple-voice synthesizer plugin
#plugin_LTLIBRARIES += plugin-simple-voice.la
#
#plugin_simple_voice_la_SOURCES =				\
#		plugins/text-to-speech/simple-voice/simple-voice.c
#
#plugin_simple_voice_la_CFLAGS  =				\
#		$(AM_CFLAGS)
#
#plugin_simple_voice_la_LDFLAGS =				\
#		-module -avoid-version
#
#plugin_simple_voice_la_LIBADD  =
#

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

if WRTC_ENABLED
# WRT media client
plugin_LTLIBRARIES += plugin-wrt-media-client.la

plugin_wrt_media_client_la_SOURCES = 				\
		plugins/client-api/wrt-media-client/wrt-media-client.c

plugin_wrt_media_client_la_CFLAGS =				\
		$(AM_CFLAGS)					\
		$(MURPHY_GLIB_CFLAGS)				\
		$(GIO_CFLAGS)

plugin_wrt_media_client_la_LDFLAGS =				\
		-module -avoid-version

plugin_wrt_media_client_la_LIBADD =				\
		$(MURPHY_GLIB_LIBS)				\
		$(GIO_LIBS)
endif

# cleanup
clean-local::
	-rm -f *~
