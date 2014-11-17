%{!?_with_debug:%{!?_without_debug:%define _with_debug 1}}
%{!?_with_sphinx:%{!?_without_sphinx:%define _with_sphinx 1}}
%{!?_with_festival:%{!?_without_festival:%define _with_festival 1}}
%{!?_with_wrt:%{!?_without_wrt:%define _with_wrt 1}}
%{!?_with_dbus:%{!?_without_dbus:%define _without_dbus 1}}

Summary: Speech recognition service for Tizen
Name: speech-recognition
Version: 0.0.9
Release: 0
License: BSD-3-Clause
Group: Base/Utilities
URL: https://github.com/otcshare/speech-recognition
Source0: %{name}-%{version}.tar.gz

BuildRequires: pkgconfig(libpulse)

BuildRequires: pkgconfig(murphy-common)
BuildRequires: pkgconfig(murphy-pulse)
BuildRequires: pkgconfig(murphy-glib)

BuildRequires: pkgconfig(libudev)
BuildRequires: pkgconfig(json)
%if %{?_with_sphinx:1}%{!?_with_sphinx:0}
BuildRequires: pkgconfig(pocketsphinx)
BuildRequires: pkgconfig(sphinxbase)
%endif
%if %{?_with_festival:1}%{!?_with_festival:0}
BuildRequires: festival-devel
Requires: festival
%endif
%if %{?_with_dbus:1}%{!?_with_dbus:0}
BuildRequires: pkgconfig(dbus-1)
%endif
Requires: pulseaudio
%if %{?_with_sphinx:1}%{!?_with_sphinx:0}
Requires: sphinxbase
Requires: pocketsphinx
%endif
BuildRequires: pkgconfig(libsystemd-daemon)
BuildRequires: pkgconfig(glib-2.0)

%description
SRS/Winthorpe speech recognition system service.

%package devel
Summary: The header files and libraries needed for SRS/Winthorpe clients
Group: Development/Libraries
Requires: %{name} = %{version}

%description devel
This package contains header files and libraries necessary for development.

%package tests
Summary: Various test binaries for SRS/Winthorpe
Group: Development/Debug
Requires: %{name} = %{version}

%description tests
This package contains various test binaries for SRS/Winthorpe.

%prep
%setup -q -n %{name}-%{version}

%build
%if %{?_with_debug:1}%{!?_with_debug:0}
export CFLAGS="-O0 -g3"
export CXXFLAGS="-O0 -g3"
V="V=1"
%endif

CONFIG_OPTIONS=""

%if %{?_with_sphinx:1}%{!?_with_sphinx:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-sphinx"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-sphinx"
%endif

%if %{?_with_festival:1}%{!?_with_festival:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-festival"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-festival"
%endif

%if %{?_with_wrt:1}%{!?_with_wrt:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-wrt-client"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-wrt-client"
%endif

%if %{?_with_dbus:1}%{!?_with_dbus:0}
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-gpl --enable-dbus"
%else
CONFIG_OPTIONS="$CONFIG_OPTIONS --disable-dbus"
%endif

CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-w3c-speech"
CONFIG_OPTIONS="$CONFIG_OPTIONS --enable-systemd"

./bootstrap && \
    %configure $CONFIG_OPTIONS && \
    make

%install
rm -fr $RPM_BUILD_ROOT

%make_install

# Install dictionaries, configuration and service files.
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir} \
    $RPM_BUILD_ROOT%{_sysconfdir}/speech-recognition \
    $RPM_BUILD_ROOT%{_sysconfdir}/speech-recognition/w3c-grammars \
    $RPM_BUILD_ROOT%{_unitdir_user} \
    $RPM_BUILD_ROOT%{_datadir}/speech-recognition/dictionaries/demo \
    $RPM_BUILD_ROOT%{_libdir}/srs/scripts \
    $RPM_BUILD_ROOT%{_datadir}/dbus-1/services

cat packaging/speech-recognition.conf.in | \
    sed "s#@DATADIR@#%{_datadir}#g" \
        > packaging/speech-recognition.conf
cat packaging/speech-recognition.service.in | \
    sed "s#@LIBDIR@#%{_libdir}#g" \
        > packaging/speech-recognition.service

/usr/bin/install -m 644 packaging/speech-recognition.conf \
    $RPM_BUILD_ROOT%{_sysconfdir}/speech-recognition

/usr/bin/install -m 644 packaging/speech-recognition.service \
    $RPM_BUILD_ROOT%{_unitdir_user}

/usr/bin/install -m 644 packaging/speech-recognition.socket \
    $RPM_BUILD_ROOT%{_unitdir_user}
/usr/bin/install -m 644 \
    -t $RPM_BUILD_ROOT%{_datadir}/speech-recognition/dictionaries/demo \
    dictionaries/demo/demo.*
/usr/bin/install -m 755 packaging/start-speech-service.sh \
     $RPM_BUILD_ROOT%{_libdir}/srs/scripts

/usr/bin/install -m 755 packaging/org.tizen.srs.service \
     $RPM_BUILD_ROOT%{_datadir}/dbus-1/services

mkdir -p $RPM_BUILD_ROOT%{_datadir}/speech-recognition/dictionaries/w3c-speech

%install_service ../user/weston.target.wants speech-recognition.socket

%clean
rm -rf $RPM_BUILD_ROOT

%post
ldconfig
%systemd_post speech-recognition.service

%preun
%systemd_preun speech-recognition.service

%postun
ldconfig
%systemd_postun speech-recognition.service

%files
%defattr(-,root,root,-)
%{_sbindir}/srs-daemon
%{_libdir}/srs
%{_libdir}/libsrs*.so.*
# crosswalk speech extension.
%{_libdir}/tizen-extensions-crosswalk/*
%{_sysconfdir}/speech-recognition/speech-recognition.conf
%dir %{_sysconfdir}/speech-recognition/w3c-grammars
%{_datadir}/speech-recognition/dictionaries
%dir %{_datadir}/speech-recognition/dictionaries/w3c-speech
%{_unitdir_user}/speech-recognition.service
%{_unitdir_user}/speech-recognition.socket
%{_unitdir_user}/weston.target.wants/speech-recognition.socket

%{_datadir}/dbus-1/services/org.tizen.srs.service

%files devel
%defattr(-,root,root,-)
%{_includedir}/srs
%{_libdir}/libsrs*.so
%{_libdir}/pkgconfig/srs*.pc

%files tests
%defattr(-,root,root,-)
%{_bindir}/srs-native-client
%{_bindir}/srs-w3c-client
%if %{?_with_dbus:1}%{!?_with_dbus:0}
%{_bindir}/srs-client
%endif
