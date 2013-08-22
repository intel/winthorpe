Summary: Speech recognition service for Tizen
Name: speech-recognition
Version: 0.0.1
Release: 0
License: BSD-3-Clause
Group: Base/Utilities
URL: https://github.com/otcshare/speech-recognition
Source0: %{name}-%{version}.tar.gz

BuildRequires: pkgconfig(pocketsphinx)
BuildRequires: pkgconfig(sphinxbase)
BuildRequires: pkgconfig(libpulse)
BuildRequires: pkgconfig(murphy-common)
BuildRequires: pkgconfig(murphy-pulse)
BuildRequires: pkgconfig(murphy-glib)
BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(libudev)
BuildRequires: pkgconfig(json)

Requires: pulseaudio
Requires: sphinxbase
Requires: pocketsphinx

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
./bootstrap && \
    %configure --disable-gpl --disable-dbus \
        --enable-sphinx --enable-wrt-client && \
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
%{_libdir}/srs
%{_sysconfdir}/speech-recognition/speech-recognition.conf
%{_sysconfdir}/sysconfig/speech-recognition
%{_datadir}/speech-recognition/dictionaries
/lib/systemd/user/speech-recognition.service

%files doc
%defattr(-,root,root,-)
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
