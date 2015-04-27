%bcond_with wayland
Name:       e-mod-tizen-wl-textinput
Summary:    The Enlightenment WM Wayland Text Input Module for Tizen
Version:    0.1.1
Release:    1
Group:      Graphics & UI Framework/Other
License:    BSD 2-Clause and MIT
Source0:    %{name}-%{version}.tar.bz2
BuildRequires: pkgconfig(wayland-server)
BuildRequires: pkgconfig(enlightenment)
%if !%{with wayland}
ExclusiveArch:
%endif

%description
The Enlightenment WM Wayland Text Input Module for Tizen

%prep
%setup -q -n %{name}-%{version}

%build
%autogen
%configure
make %{?_smp_mflags}

%install
# for license notification
mkdir -p %{buildroot}/usr/share/license
cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/usr/share/license/%{name}

# install
%make_install

%files
%defattr(-,root,root,-)
%{_libdir}/enlightenment/modules/e-mod-tizen-wl-textinput
/usr/share/license/%{name}

%define _unpackaged_files_terminate_build 0