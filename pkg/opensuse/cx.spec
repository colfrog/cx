Name:           cx
Version:        0.2
Release:        0
Summary:	A directory history management utility
License:        BSD-2-Clause
Group:          Development/Tools/Navigators
Url:            https://nilio.ca/%{name}
Source:         https://nilio.ca/%{name}/%{name}-%{version}.tar.xz
Vendor:		Laurent Cimon <laurent@nilio.ca>
Packager:	Laurent Cimon <laurent@nilio.ca>
BuildRequires:  sqlite3-devel

%description
cx is a directory history management utility written in C.
It hooks itself to your cd command in order to maintain
an SQLite database of recent directories you've gone through,
in order to let you jump to past directories with the cx command
using a combination of frecency and POSIX regex.

%prep
%setup -q -n %{name}-%{version}

%build
%make_build

%install
make PREFIX=%{buildroot}%{_prefix} install

%files
%license LICENSE
%doc README.md
%{_bindir}/cxc
%{_bindir}/cxd
%{_prefix}/share/cx/*

%changelog
