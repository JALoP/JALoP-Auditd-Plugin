Name:           jalauditd
Version:        0.0.1
Release:        1
Summary:        JAL Audit Plugin
Group:          Applications/Plugin
License:        Apache License, Version 2.0
Source:         %{name}-%{version}-%{release}.tar.gz
BuildRoot:      %{_tmppath}/%{name}

%description
This package contains the JAL audit plugin for audisp.

%prep
%setup -q -n %{name}


%build
make


%install
make install PREFIX=$RPM_BUILD_ROOT


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%doc
/sbin/*
/etc/*
/etc/audisp/plugins.d/*

%changelog
