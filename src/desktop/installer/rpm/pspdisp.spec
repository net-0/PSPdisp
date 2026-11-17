# SPDX-License-Identifier: BSD-3-Clause
#
# pspdisp.spec - Spec of the RPM of PSPdisp (Fedora)
#
# Copyright © 2026 Arthur Neto
#
# Read by rpmbuild when `make` makes the RPM (see rpm.mk), which gives it the version (pspdisp_version), the release (pspdisp_release) and the folder of the files it packages, staged as they're installed (pspdisp_stage_dir).
# rpmbuild finds the libraries the programs link by itself, and makes them dependencies (e.g. libgtk-4.so.1).
#
# The other dependencies are what the programs run but don't link:
# - the EVDI kernel module: evdi-kmod, which akmod-evdi provides (built for each kernel), from the fedora-multimedia repository of negativo17.org, as Fedora has no EVDI (its libevdi provides the library the daemon links)
# - PipeWire, for the sound
# - polkit (its pkexec), so it asks for the password to create an EVDI device when none is free (e.g. a DisplayLink dock took it)

# The programs are built and stripped by make, so there's nothing to debug
%global debug_package %{nil}

# No changelog here (the history is the one of git), so the dates of the package aren't taken from it
%global source_date_epoch_from_changelog 0

Name:           pspdisp
Version:        %{pspdisp_version}
Release:        %{pspdisp_release}
Summary:        Use a PSP as a display of the computer
License:        BSD-3-Clause
URL:            https://github.com/net-0/PSPdisp
Requires:       evdi-kmod
Recommends:     pipewire
Recommends:     polkit

%description
PSPdisp turns a PSP into a second display of the computer: the PSP shows a
virtual display, plays the desktop sound, and its buttons drive a virtual
gamepad, keyboard or mouse.

This package has the desktop app (pspdisp-ui) and the daemon that streams to
the PSP (pspdisp-daemon). The PSP app is installed on the PSP itself.

%install
cp -a %{pspdisp_stage_dir}/. %{buildroot}

# Applies what the package installs at once, without a restart:
# - the udev rule, which udev only reads again at boot: to /dev/uinput, /dev/uhid, and to a PSP already plugged (any Sony USB device, the rule picks the PSP)
# - the EVDI module, loaded at boot (see display/connector/evdi/modules-load.conf), so it creates its device now (unless it's already loaded, e.g. for a DisplayLink dock)
# Only on a running system (where udev runs), not in a chroot or a container; what fails is left to the next boot (e.g. akmods may still be building the module).
%post
if [ -S /run/udev/control ]; then
	udevadm control --reload-rules || :
	udevadm trigger --subsystem-match=misc --sysname-match=uinput || :
	udevadm trigger --subsystem-match=misc --sysname-match=uhid || :
	udevadm trigger --subsystem-match=usb --attr-match=idVendor=054c || :
	modprobe evdi || :
fi

# The autostart entry is a configuration file, as the files of /etc, so a changed one is kept on upgrade
%files
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/pspdisp-ui
%{_bindir}/pspdisp-daemon
%{_datadir}/applications/com.pspdisp.ui.desktop
%{_datadir}/icons/hicolor/scalable/apps/com.pspdisp.ui.svg
%{_prefix}/lib/udev/rules.d/60-pspdisp.rules
%{_prefix}/lib/modprobe.d/00-pspdisp.conf
%{_prefix}/lib/modules-load.d/pspdisp.conf
%config(noreplace) %{_sysconfdir}/xdg/autostart/com.pspdisp.daemon.desktop
