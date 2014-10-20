#######  defines  ###########
%{!?package_name: %define package_name primdns}
%{!?package_version: %define package_version SNAPSHOT}
%{!?install_prefix: %define install_prefix /usr}
%{!?sysconf_install_prefix: %define sysconf_install_prefix /}
#############################

Name: %{package_name}
Version: %{package_version}
Release: 1
Group: System Environment/Daemons
Vendor: Satoshi Ebisawa <ebisawa@gmail.com>
Packager: Satoshi Ebisawa <ebisawa@gmail.com>
URL: http://github.com/ebisawa/primdns
License: BSD
Summary: A simple DNS contents server
Source: %{package_name}-%{package_version}.tar.gz

%description

%prep

%setup

%build
./configure --prefix=$RPM_BUILD_ROOT/%{install_prefix} --sysconfdir=$RPM_BUILD_ROOT/%{sysconf_install_prefix}/etc/primdns
make

%install
make install

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
