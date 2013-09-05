%{!?_with_debug:%{!?_without_debug:%define _with_debug 1}}
%{!?_with_sphinx:%{!?_without_sphinx:%define _with_sphinx 1}}
%{!?_with_festival:%{!?_without_festival:%define _with_festival 1}}
%{!?_with_wrt:%{!?_without_wrt:%define _with_wrt 1}}
%{!?_with_dbus:%{!?_without_dbus:%define _without_dbus 1}}

Summary: Speech recognition service for Tizen
Name: speech-recognition
Version: 0.0.1
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
%endif
%if %{?_with_dbus:1}%{!?_with_dbus:0}
BuildRequires: pkgconfig(dbus-1)
%endif

Requires: pulseaudio
%if %{?_with_sphinx:1}%{!?_with_sphinx:0}
Requires: sphinxbase
Requires: pocketsphinx
%endif
%if %{?_with_festival:1}%{!?_with_festival:0}
BuildRequires: festival-devel
Requires: festival
%endif


%description
SRS/Winthorpe speech recognition system service.

%package doc
Summary: Documentation
Group: Development/Tools

%description doc
Documentation for the speech recognition service.

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


./bootstrap && \
    %configure $CONFIG_OPTIONS && \
    make

%install
rm -fr $RPM_BUILD_ROOT

%make_install

# Install dictionaries, configuration and service files.
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig \
    $RPM_BUILD_ROOT/lib/systemd/user \
    $RPM_BUILD_ROOT%{_sysconfdir}/speech-recognition \
    $RPM_BUILD_ROOT%{_datadir}/speech-recognition/dictionaries/demo

/usr/bin/install -m 644 packaging/speech-recognition.conf \
    $RPM_BUILD_ROOT%{_sysconfdir}/speech-recognition
/usr/bin/install -m 644 packaging/speech-recognition.env \
    $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/speech-recognition
/usr/bin/install -m 644 packaging/speech-recognition.service \
    $RPM_BUILD_ROOT/lib/systemd/user
/usr/bin/install -m 644 \
    -t $RPM_BUILD_ROOT%{_datadir}/speech-recognition/dictionaries/demo \
    dictionaries/demo/demo.*

%clean
rm -rf $RPM_BUILD_ROOT

%post
ldconfig

%postun
ldconfig

%files
%defattr(-,root,root,-)
%{_sbindir}/srs-daemon
%if %{?_with_dbus:1}%{!?_with_dbus:0}
%{_bindir}/srs-client
%endif
%{_libdir}/srs
%{_sysconfdir}/speech-recognition/speech-recognition.conf
%{_sysconfdir}/sysconfig/speech-recognition
%{_datadir}/speech-recognition/dictionaries
/lib/systemd/user/speech-recognition.service

%files doc
%defattr(-,root,root,-)
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
