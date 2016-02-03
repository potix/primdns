#######  defines  ###########
%{!?package_name: %define package_name primdns}
%{!?package_version: %define package_version SNAPSHOT}
%{!?install_prefix: %define install_prefix /usr}
%{!?sysconf_install_prefix: %define sysconf_install_prefix /}
#############################

Name:     %{package_name}
Version:  %{package_version}
Release:  1%{?dist}
Group:    System Environment/Daemons
Vendor:   Satoshi Ebisawa <ebisawa@gmail.com>
Packager: Hiroyuki Kakine <poti.dog@gmail.com>
URL:      https://github.com/potix/primdns
License:  BSD
Summary:  A simple DNS contents server
Source:   %{package_name}-%{package_version}.tar.gz
Requires: ruby
Prefix:   /

%description

%prep

%setup

%build
./configure --prefix=$RPM_BUILD_ROOT%{install_prefix} --sysconfdir=$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/primdns
make

%install
make install
%if 7%{?rhl}
mkdir -p "$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/systemd/system"
%else
mkdir -p "$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/init.d"
%endif
mkdir -p "$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/sysconfig"
%if 7%{?rhl}
install -c -m 755 "%{_builddir}/%{package_name}-%{package_version}/primd/rc/primd.service" "$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/systemd/system"
%else
install -c -m 755 "%{_builddir}/%{package_name}-%{package_version}/primd/rc/primd.init.sh" "$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/init.d/primd"
%endif
install -c -m 644 "%{_builddir}/%{package_name}-%{package_version}/primd/rc/primd.sysconfig" "$RPM_BUILD_ROOT%{sysconf_install_prefix}/etc/sysconfig/primd"

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{install_prefix}/sbin/primd
%{install_prefix}/sbin/primdns-axfr
%{install_prefix}/sbin/primdns-makedb
%{install_prefix}/sbin/primdns-updatezone
%config(noreplace) %{sysconf_install_prefix}/etc/primdns/0.0.127.in-addr.arpa.zone
%config(noreplace) %{sysconf_install_prefix}/etc/primdns/localhost.tiny
%config(noreplace) %{sysconf_install_prefix}/etc/primdns/localhost.zone
%config(noreplace) %{sysconf_install_prefix}/etc/primdns/primd.conf
%config(noreplace) %{sysconf_install_prefix}/etc/primdns/primd.conf
%if 7%{?rhl}
%{sysconf_install_prefix}/etc/systemd
%else
%{sysconf_install_prefix}/etc/init.d
%endif
%config(noreplace) %{sysconf_install_prefix}/etc/sysconfig
