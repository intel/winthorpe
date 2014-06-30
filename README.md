Winthorpe - Let Your Applications Listen... And Talk
====================================================

What is Winthorpe ?
-------------------

Winthorpe is a platform service for speech recognition and synthesis.
Its main goal is to provide a framework for speech/voice enabling
applications. It aims to provide an easy to use but versatile API to
liberate developers from the low-level implementation details of speech
recognition and synthesis, allowing them to instead focus their full
attention on how to improve the usability of their applications by
utilizing these new interaction mechanisms with the end user.

Winthorpe does not contain a recognition or synthesis engine as such.
We stand on the shoulders of giants and rely for these tasks on the
excellent work other folks have been and keep doing in these areas.
Winthorpe provides the necessary mechanisms that allows existing engines
to be plugged in as recognition and synthesis backends. Winthorpe already
contains plugins for using CMU pocketsphinx for recognition and espeak
and/or festival for synthesis.

Winthorpe can also utilize Murphy, the scriptable resource policy
framework, to arbitrate between and bring context-awareness to speech-
enabled applications. If configured so, Winthorpe will let Murphy make
decisions about which applications and when are allowed to actively use
the speech services. The Murphy policy configuration can then be used to
resolve conflicts between applications that are otherwise completely
unaware of each other. Moreover the policies can be used to dynamically
enable and disable speech services, both globally or just for a subset of
applications, adapting the speech subsystem intelligently to the context
and overall state of the system.

Our idealistic long-term goal for Winthorpe is to build step-by-step the
framework and using the framework a context-aware voice enabled personal
assistant which allows relatively straightforward integration of new
applications.

We realize that these goals are ambitious, far from being straightforward
and we have only taken the first few tiptoeing baby steps experimenting
with our ideas to achieve some of them. If you are interested in speech
recognition, synthesis, or speech-enabling your application and would like
to help us, please don't hesitate to contact us.


Getting Winthorpe Up And Running
--------------------------------

Winthorpe itself is hosted on github at http://github.com/01org/winthorpe.
You can clone Winthorpe using git with the following command:

    git clone git@github.com:01org/winthorpe.git

Additionally you will need the following prerequisites to get Winthorpe
up and running with a reasonable set of plugins:

Murphy

Winthorpe reuses parts of Murphy for a large part of its basic infra. Thus
you will need to get Murphy to compile Winthorpe. Murphy is hosted on
github at http://github.com/01org/murphy. You can clone it with

    git clone git@github.com:01org/murphy.git

See the (arguably sparse) Murphy documentation on how to compile it.

PulseAudio

The existing Winthorpe recognizer and synthesizer backends use PulseAudio
for recording and rendering audio. Your distribution should provide packages
both for the daemon and the necessary client libraries.

For instance for Fedora you'd need pulseaudio, pulseaudio-libs and
pulseaudio-libs-devel (along with their dependencies).

CMU Pocketsphinx

Winthorpe provides a plugin that uses CMU Pocketsphinx as a speech
recognition backend. Most desktop linux distributions provide packages
for sphinxbase, pocketsphinx and some language models and dictionaries.
Winthorpe should work both with version 0.7 and 0.8 of pocketsphinx.

For instance for Fedora you'd need sphinxbase-libs, sphinxbase-devel,
pocketsphinx, pocketsphinx-devel, and pocketsphinx-models (along with
their dependencies).

Espeak/flite

Winthorpe has a synthesizer backend based on espeak. If you want to play
around with synthesizing, you will either need this or festival. Espeak
usually provides voices for more languages than festival and the espeak
backend is also a bit more versatile than the festival based one (which
does not support voice pitch or rate control). Most desktop linux
distributions provide packages for espeak.

For instance for Fedora you'd need espeak and espeak-devel (along with
their dependencies).

Festival

Winthorpe has a synthesizer plugin that uses festival as the backend.
If you want to play around with synthesizing, you might want to install
this. You can load several synthesizer backends simulatanously to
Winthorpe. They usually have different level of support for different
languages, so if you want support for as many languages as possible,
you should enable and load as many of them as you can (IOW, both as of
now). Most desktop distributions provide packages for festival.

For instance on Fedora you'd need festival, festival-lib, and
festival-devel (along with their dependencies).

libdbus

If you plan to use the D-Bus client API, you'll need libdbus. For
instance on Fedora you'd need dbus-devel (along with its dependencies).
If you choose to enable D-Bus support, don't forget to enable it also
in Murphy.

GLib

Currently you need to install GLib (glib-2.0 and gobject-2.0) to compile
Withorpe, although none of Winthorpes core, or core plugins use GLib in
any way. The only plugin utilizing glib (via gdbus) is the demo Tizen WRT
media player client plugin. The current build system checks for glib and
gobject regardless of whether this plugin is enabled or not. This will be
fixed in the future. However, since many of Winthropes the dependencies
pull in GLib on desktop distros anyway this should not be an insurmountable
problem for now...

For instance on Fedora you'd need glib2-devel (along with its dependencies).

Systemd

Additionally if you feel adventorous and plan to install Winthorpe and
use systemd's socket-based activation as the mechanism to start Winthorpe
you will need libsystemd-daemon from systemd.

For instance on Fedora you'd need systemd-devel (along with its dependencies).


Configuring And Compiling

Once you have all the prerequisites installed you should compile Winthorpe.
If you have installed all of the above, you can do this by running the
following sequence of commands in the top Winthorpe directory:

    ./bootstrap --prefix=/usr --sysconfdir=/etc --enable-gpl --enable-dbus \
                --enable-sphinx --enable-espeak --enable-festival \
                --enable-systemd

    make

If everything goes well, you should end up with the Winthorpe daemon and a
set of Winthorpe plugins successfully compiled. You can start up and test
the daemon without installing it with the following command:

    ./src/srs-daemon -P `pwd`/src/.libs -c speech-recognition.conf -f -vvv \
        -s sphinx.pulsesrc=alsa_input.pci-0000_00_1b.0.analog-stereo

Just replace the value sphinx.pulsesrc is set to with the name of the
PulseAudio source corresponding to the mike you want to use. That's it,
now you should have the daemon up and running. You can try synthesis
for instance with the native client like this:

    ./src/srs-native-client
    Using pa_manloop...
    disconnected>
    Connection to server established.
    connected> list voices
    ...
    connected> render tts "Is this able to speak now ?" -events
    0.000000 % (0 msec) of TTS #3 rendered...
    13.874767 % (249 msec) of TTS #3 rendered...
    19.852016 % (357 msec) of TTS #3 rendered...
    ...
    100.000000 % (1801 msec) of TTS #3 rendered...
    Rendering of TTS #3 completed.
    connected> render tts "Is this able to speak now ?" -events -voice:english-british-male-1
    Rendering of TTS #4 started...
    0.000000 % (0 msec) of TTS #4 rendered...
    13.870927 % (249 msec) of TTS #4 rendered...
    ...
    98.981004 % (1783 msec) of TTS #4 rendered...
    100.000000 % (1802 msec) of TTS #4 rendered...
    Rendering of TTS #4 completed.
    connected>
