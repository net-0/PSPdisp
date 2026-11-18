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

# Stops the daemons run from the package, of every user, however they started (at sign-in, by the app or by hand), as replacing or removing its files leaves them running (see %pre and %preun)
# Also the ones whose file an older upgrade replaced while they ran
%global stop_daemons %{expand:
for pid in $(pgrep -x pspdisp-daemon); do
	case "$(readlink "/proc/$pid/exe")" in
		/usr/bin/pspdisp-daemon|'/usr/bin/pspdisp-daemon (deleted)') ;;
		*) continue ;;
	esac
	# Asked to stop as with Ctrl+C (SIGTERM), which it does at once, and ended if it still runs after 5 seconds
	kill "$pid" 2>/dev/null || continue
	for _ in $(seq 50); do
		kill -0 "$pid" 2>/dev/null || continue 2
		sleep 0.1
	done
	kill -KILL "$pid" 2>/dev/null || true
done
}

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

# Stops the daemons before an upgrade ($1 is 2, the versions installed once it's done), so %post starts the new one, instead of the old one running on until the next sign-in
# Not in the %preun of the old version, which runs once the %post of the new one is done
# Only on a running system (where systemd runs), not in a chroot or a container, the same as %post
%pre
if [ "$1" -gt 1 ] && [ -d /run/systemd/system ]; then
%{stop_daemons}
fi

# Applies what the package installs at once, without a restart:
# - the udev rule, which udev only reads again at boot: to /dev/uinput, /dev/uhid, and to a PSP already plugged (any Sony USB device, the rule picks the PSP)
# - the EVDI module, loaded at boot (see display/connector/evdi/modules-load.conf), so it creates its device now (unless it's already loaded, e.g. for a DisplayLink dock)
# - the daemon, for each user signed in to a desktop, started at their sign-in: the unit systemd-xdg-autostart-generator makes of its autostart entry (see daemon/com.pspdisp.daemon.desktop) once the systemd of the user reloads, none when they turned it off
# Only on a running system (where udev and systemd run), not in a chroot or a container; what fails is left to the next boot (e.g. akmods may still be building the module), or sign-in.
%post
if [ -S /run/udev/control ]; then
	udevadm control --reload-rules || :
	udevadm trigger --subsystem-match=misc --sysname-match=uinput || :
	udevadm trigger --subsystem-match=misc --sysname-match=uhid || :
	udevadm trigger --subsystem-match=usb --attr-match=idVendor=054c || :
	modprobe evdi || :
fi

# Only the sessions of the users at a desktop, not the others (e.g. SSH, or the one of the login screen)
if [ -d /run/systemd/system ]; then
	for session in $(loginctl list-sessions --no-legend | awk '{ print $1 }'); do
		[ "$(loginctl show-session "$session" -p Class --value)" = user ] || continue
		case "$(loginctl show-session "$session" -p Type --value)" in
			x11|wayland) ;;
			*) continue ;;
		esac
		user=$(loginctl show-session "$session" -p Name --value) || continue
		systemctl --user -M "$user@" daemon-reload || continue
		# Quiet, as the unit is missing when the user turned the autostart off
		systemctl --user -M "$user@" start --no-block app-com.pspdisp.daemon@autostart.service 2>/dev/null || :
	done
fi

# Stops the daemons before a removal ($1 is 0, the versions left once it's done), not on upgrade (see %pre)
%preun
if [ "$1" -eq 0 ] && [ -d /run/systemd/system ]; then
%{stop_daemons}
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
