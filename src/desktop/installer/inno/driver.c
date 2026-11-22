/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * driver.c - Driver packages of the installer       *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Installs and removes the driver packages of PSPdisp (Windows), for the installer (see pspdisp.iss), as an administrator:
 *
 *   pspdisp-driver install <INF> <hardware ID>
 *   pspdisp-driver uninstall <INF name> <hardware ID>
 *
 * Each driver is for a root device, which no bus enumerates (the IddCx display and the virtual gamepad), known by its hardware ID:
 * "install" creates the device when there's none (as `devcon install` does, which the WDK doesn't let us ship), then installs the driver on it,
 * and "uninstall" removes the device.
 *
 * "uninstall" also deletes every package of that INF from the driver store (an INF is renamed oem<N>.inf there, so it's found by its original name).
 *
 * It prints what failed, and exits with 0, 1 when it failed, or 3010 (`ERROR_SUCCESS_REBOOT_REQUIRED`) when Windows has to restart to finish.
 */

#include <winsock2.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h> // MAX_CLASS_NAME_LEN and MAX_DEVICE_ID_LEN
#include <newdev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define PSPDISP_DRIVER_EXIT_FAILED 1 // Exit status when it failed

/**
 * @brief Print an error with the message of a Windows error code.
 *
 * @param what What failed.
 * @param code The Windows error code (e.g. from `GetLastError()`).
 */
void pspdisp_driver_print_error(const wchar_t *what, DWORD code) {
	wchar_t *message = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, (wchar_t *) &message, 0, NULL);
	wprintf(L"%ls: error 0x%08lX %ls\n", what, code, message != NULL ? message : L"");
	LocalFree(message);
}

/**
 * @brief Tell if a device has a hardware ID.
 *
 * @param set The device information set of the device.
 * @param device The device.
 * @param hardware_id The hardware ID, compared without case.
 * @return `true` if it's one of the hardware IDs of the device.
 */
bool pspdisp_driver_has_hardware_id(HDEVINFO set, SP_DEVINFO_DATA *device, const wchar_t *hardware_id) {
	wchar_t ids[1024] = {0}; // A list of strings, ended by an empty one
	if (!SetupDiGetDeviceRegistryPropertyW(set, device, SPDRP_HARDWAREID, NULL, (BYTE *) ids, sizeof(ids) - 2 * sizeof(wchar_t), NULL)) return false;

	for (const wchar_t *id = ids; *id != L'\0'; id += wcslen(id) + 1) {
		if (_wcsicmp(id, hardware_id) == 0) return true;
	}
	return false;
}

/**
 * @brief Call a function on each device of a hardware ID.
 *
 * @param hardware_id The hardware ID.
 * @param action Called with each device, it returns `false` if it failed.
 * @param count Where to write the number of devices found, or `NULL`.
 * @param reboot Set to `TRUE` by the action when Windows has to restart.
 * @return `true` if every action succeeded (or there was no device).
 */
bool pspdisp_driver_for_each_device(const wchar_t *hardware_id, bool (*action)(HDEVINFO, SP_DEVINFO_DATA *, BOOL *), int *count, BOOL *reboot) {
	HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES);
	if (set == INVALID_HANDLE_VALUE) {
		pspdisp_driver_print_error(L"Failed to list the devices", GetLastError());
		return false;
	}

	bool succeeded = true;
	int found = 0;
	SP_DEVINFO_DATA device = { .cbSize = sizeof(device) };
	for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &device); i++) {
		if (!pspdisp_driver_has_hardware_id(set, &device, hardware_id)) continue;

		found++;
		if (action != NULL && !action(set, &device, reboot))
			succeeded = false;
	}

	SetupDiDestroyDeviceInfoList(set);
	if (count != NULL)
		*count = found;
	return succeeded;
}

/**
 * @brief Remove a device, with its driver (`pspdisp_driver_for_each_device()` action).
 *
 * @param set The device information set of the device.
 * @param device The device.
 * @param reboot Set to `TRUE` when Windows has to restart.
 * @return `true` if it was removed.
 */
bool pspdisp_driver_remove_device(HDEVINFO set, SP_DEVINFO_DATA *device, BOOL *reboot) {
	// As `devcon remove` (`DiUninstallDevice()` isn't in the import library of MinGW)
	if (!SetupDiCallClassInstaller(DIF_REMOVE, set, device)) {
		pspdisp_driver_print_error(L"Failed to remove the device", GetLastError());
		return false;
	}

	SP_DEVINSTALL_PARAMS_W params = { .cbSize = sizeof(params) };
	if (SetupDiGetDeviceInstallParamsW(set, device, &params) && (params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)))
		*reboot = TRUE;
	return true;
}

/**
 * @brief Create a root device, which no bus enumerates, for a driver to be installed on it.
 *
 * Its instance ID is ROOT\<class of the INF>\<number>, as `devcon install` makes it (e.g. ROOT\DISPLAY\0000).
 *
 * @param inf The full path of the INF of its driver, whose class it takes.
 * @param hardware_id Its hardware ID, the one the INF matches.
 * @return `true` if it was created.
 */
bool pspdisp_driver_create_root_device(const wchar_t *inf, const wchar_t *hardware_id) {
	GUID class_guid;
	wchar_t class_name[MAX_CLASS_NAME_LEN];
	if (!SetupDiGetINFClassW(inf, &class_guid, class_name, MAX_CLASS_NAME_LEN, NULL)) {
		pspdisp_driver_print_error(L"Failed to read the class of the INF", GetLastError());
		return false;
	}

	HDEVINFO set = SetupDiCreateDeviceInfoList(&class_guid, NULL);
	if (set == INVALID_HANDLE_VALUE) {
		pspdisp_driver_print_error(L"Failed to create the device list", GetLastError());
		return false;
	}

	// The hardware IDs are a list of strings, ended by an empty one
	wchar_t ids[MAX_DEVICE_ID_LEN + 2] = {0};
	wcsncpy(ids, hardware_id, MAX_DEVICE_ID_LEN);

	SP_DEVINFO_DATA device = { .cbSize = sizeof(device) };
	bool created = false;
	if (!SetupDiCreateDeviceInfoW(set, class_name, &class_guid, NULL, NULL, DICD_GENERATE_ID, &device))
		pspdisp_driver_print_error(L"Failed to create the device", GetLastError());
	else if (!SetupDiSetDeviceRegistryPropertyW(set, &device, SPDRP_HARDWAREID, (const BYTE *) ids, (wcslen(ids) + 2) * sizeof(wchar_t)))
		pspdisp_driver_print_error(L"Failed to set the hardware ID of the device", GetLastError());
	else if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &device))
		pspdisp_driver_print_error(L"Failed to register the device", GetLastError());
	else
		created = true;

	SetupDiDestroyDeviceInfoList(set);
	return created;
}

/**
 * @brief Install a driver package on its root device, created when there's none.
 *
 * @param inf The path of the INF.
 * @param hardware_id The hardware ID of its root device.
 * @param reboot Set to `TRUE` when Windows has to restart.
 * @return `true` if it was installed.
 */
bool pspdisp_driver_install(const wchar_t *inf, const wchar_t *hardware_id, BOOL *reboot) {
	wchar_t path[MAX_PATH];
	if (GetFullPathNameW(inf, MAX_PATH, path, NULL) == 0) {
		pspdisp_driver_print_error(L"Failed to find the INF", GetLastError());
		return false;
	}

	// Its device first, once (the next installs update its driver)
	int count = 0;
	pspdisp_driver_for_each_device(hardware_id, NULL, &count, reboot);
	if (count == 0 && !pspdisp_driver_create_root_device(path, hardware_id)) return false;

	// Installed on the device even if its driver is newer (e.g. an older version of PSPdisp installed again)
	if (!UpdateDriverForPlugAndPlayDevicesW(NULL, hardware_id, path, INSTALLFLAG_FORCE, reboot)) {
		pspdisp_driver_print_error(L"Failed to install the driver on its device", GetLastError());
		return false;
	}
	return true;
}

/**
 * @brief Remove a driver package: its root device, then its packages from the driver store.
 *
 * @param inf_name The name of the INF, as built (e.g. pspdisp-iddcx.inf), which the driver store keeps as its original name.
 * @param hardware_id The hardware ID of its root device.
 * @param reboot Set to `TRUE` when Windows has to restart.
 * @return `true` if it was removed (or wasn't installed).
 */
bool pspdisp_driver_uninstall(const wchar_t *inf_name, const wchar_t *hardware_id, BOOL *reboot) {
	bool succeeded = pspdisp_driver_for_each_device(hardware_id, pspdisp_driver_remove_device, NULL, reboot);

	// The packages in the driver store are the oem<N>.inf of the INF folder of Windows
	wchar_t folder[MAX_PATH], pattern[MAX_PATH];
	GetWindowsDirectoryW(folder, MAX_PATH);
	wcscat(folder, L"\\INF\\");
	_snwprintf(pattern, MAX_PATH, L"%lsoem*.inf", folder);

	WIN32_FIND_DATAW found;
	HANDLE search = FindFirstFileW(pattern, &found);
	if (search == INVALID_HANDLE_VALUE) return succeeded;

	do {
		wchar_t path[MAX_PATH];
		_snwprintf(path, MAX_PATH, L"%ls%ls", folder, found.cFileName);

		// Its original name, from the information of the INF
		DWORD size = 0;
		if (!SetupGetInfInformationW(path, INFINFO_INF_NAME_IS_ABSOLUTE, NULL, 0, &size)) continue;
		SP_INF_INFORMATION *information = malloc(size);
		SP_ORIGINAL_FILE_INFO_W original = { .cbSize = sizeof(original) };
		bool ours = SetupGetInfInformationW(path, INFINFO_INF_NAME_IS_ABSOLUTE, information, size, NULL)
			&& SetupQueryInfOriginalFileInformationW(information, 0, NULL, &original)
			&& _wcsicmp(original.OriginalInfName, inf_name) == 0;
		free(information);

		// Deleted even if a device still uses it (e.g. its root device couldn't be removed)
		if (ours && !SetupUninstallOEMInfW(found.cFileName, SUOI_FORCEDELETE, NULL)) {
			pspdisp_driver_print_error(L"Failed to delete the driver from the driver store", GetLastError());
			succeeded = false;
		}
	} while (FindNextFileW(search, &found));

	FindClose(search);
	return succeeded;
}

/**
 * @brief Install or remove a driver package (see the top of this file).
 *
 * @param argc The number of arguments of the command line.
 * @param argv The arguments of the command line.
 * @return 0 if it succeeded, 1 if it failed, or 3010 when Windows has to restart to finish.
 */
int wmain(int argc, wchar_t **argv) {
	if (argc != 4 || (wcscmp(argv[1], L"install") != 0 && wcscmp(argv[1], L"uninstall") != 0)) {
		wprintf(L"Usage: pspdisp-driver install <INF> <hardware ID>\n       pspdisp-driver uninstall <INF name> <hardware ID>\n");
		return PSPDISP_DRIVER_EXIT_FAILED;
	}

	BOOL reboot = FALSE;
	bool succeeded = wcscmp(argv[1], L"install") == 0
		? pspdisp_driver_install(argv[2], argv[3], &reboot)
		: pspdisp_driver_uninstall(argv[2], argv[3], &reboot);

	if (!succeeded) return PSPDISP_DRIVER_EXIT_FAILED;
	return reboot ? ERROR_SUCCESS_REBOOT_REQUIRED : 0;
}
