Summary: Speech recognition for Tizen
Name: speech-recognition
Version:  0.0.1
Release: 0
License: LGPLv2.1
Group: System Environment/Daemons
URL: https://github.com/otcshare/speech-recognition
Source0: %{name}-%{version}.tar.gz
#BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: pkgconfig(pocketsphinx)
BuildRequires: pkgconfig(sphinxbase)
BuildRequires: pkgconfig(libpulse)
BuildRequires: pkgconfig(murphy-common)
BuildRequires: pkgconfig(murphy-pulse)
BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(libudev)
BuildRequires: pkgconfig(json)
Requires: pulseaudio

%description
This package contains a pulseaudio module that enforces (mostly audio) routing,
corking and muting policy decisions.

%prep
%setup -q -n %{name}-%{version}

%build
./bootstrap --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib \
    --enable-gpl --enable-dbus --enable-sphinx

make

%install
rm -rf %{buildroot}
%make_install

mkdir -p %{buildroot}/usr/share/srs
cp run-speech-daemon.sh %{buildroot}/usr/share/srs
cp speech-recognition.conf %{buildroot}/usr/share/srs
cp demo/dictionary/demo.* %{buildroot}/usr/share/srs


%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
/usr/bin/srs-client
/usr/lib/src/plugins/plugin-*
/usr/sbin/srs-daemon
/usr/share/doc/speech-recognition/*
/usr/share/srs/demo.*
/usr/share/srs/speech-recognition.conf
/usr/share/srs/run-speech-daemon.sh
