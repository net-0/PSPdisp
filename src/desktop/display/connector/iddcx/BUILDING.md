# Building the IddCx display connector (Windows)

The IddCx connector has two parts, built with different toolchains:

| Part | Files | Built with | Runs in |
| --- | --- | --- | --- |
| Driver | `driver.c`, `protocol.h` | Visual Studio + WDK, on Windows | Windows' UMDF host (`WUDFHost.exe`), Session 0 |
| Connector | `../iddcx.c`, `protocol.h` | MinGW + GLib, cross-built from Linux or MSYS2 | PSPdisp, compiled in by `connector.c` |

The driver makes the virtual monitor, the connector talks to it through IOCTLs
(see `protocol.h`). Neither part has been built or tested yet, see
[Known gaps](#known-gaps).

## Driver

Needs Windows 10 or later, Visual Studio with the "Desktop development with
C++" workload, and the matching Windows Driver Kit (WDK). The WDK has the
headers (`iddcx.h`, `wdf.h`) and libraries (`IddCxStub.lib`) that MinGW
lacks.

1. Start from Microsoft's sample, [`video/IndirectDisplay`](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay)
   in Windows-driver-samples (`IddSampleDriver` project).
2. Replace `Driver.cpp` and `Driver.h` with `driver.c` and `protocol.h`, and
   compile it as C (`/TC`).
3. Link `d3d11.lib`, `dxgi.lib`, `dxguid.lib` and `avrt.lib`.
4. In the INF, rename the driver and the device (e.g. hardware ID
   `Root\PSPdispIddCx`, `pspdisp_iddcx.dll`), keep the rest of the sample
   (the `IndirectKmd` upper filter, `UmdfExtensions = IddCx0102`, ...).

### Letting the connector open the device

By default only administrators can open the device, but PSPdisp runs as a
normal user. Give users read and write access in the INF, on the
`[...NT.hw]` section of the device:

```ini
[MyDevice_Install.NT.hw]
AddReg = MyDevice_HardwareDeviceSettings, MyDevice_Security

[MyDevice_Security]
HKR,,Security,,"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;BU)"
```

### Signing and installing

Windows only loads signed drivers. For development, use test signing
(Visual Studio signs with a test certificate when building):

```bat
bcdedit /set testsigning on
:: reboot, then from an administrator prompt, with devcon.exe from the WDK tools:
devcon install pspdisp_iddcx.inf Root\PSPdispIddCx
```

To share the driver without test signing, it needs attestation signing on
Microsoft's Partner Center, which needs an EV code signing certificate.

## Connector

Needs MinGW (`sudo pacman -S mingw-w64-gcc`) and GLib for Windows, which the
Arch repos don't package (e.g. `mingw-w64-glib2` from the AUR, or MSYS2's
`mingw-w64-x86_64-glib2`).

It isn't built on its own: `connector.c` includes it on Windows, so it's
compiled into PSPdisp, which must link `-lcfgmgr32`. To check that it
compiles:

```sh
x86_64-w64-mingw32-gcc -std=gnu23 -Wall -Wextra -Wno-pragma-once-outside-header -fsyntax-only \
    $(x86_64-w64-mingw32-pkg-config --cflags glib-2.0 gio-2.0) \
    src/desktop/display/connector/iddcx.c
```

## Known gaps

- **Untested.** The driver was never compiled (no WDK on Linux), and the
  connector was only syntax-checked with MinGW, against the Linux GLib headers.
- **480x272 might be refused.** Microsoft documents no minimum mode for
  indirect displays, so it's unknown whether Windows accepts a mode this
  small. If it doesn't, the bigger modes of the EDID (960x544 and 1920x1088)
  remain.
- **The scaling down depends on the GPU.** The driver scales the frames of the
  bigger modes down to 480x272 with their mip levels (`GenerateMips()`), whose
  filter D3D11 doesn't define (a 2x2 average in practice). A GPU without mip
  generation for the frame format only sends the top left corner.
- **No dirty rectangles.** The driver sends whole frames (about 520 KB each).
- **Power modes are approximated.** Indirect displays get no DPMS from
  Windows: the swap chain is unassigned when the monitor turns off, sent as
  DPMS off, and assigned again when it turns on, sent as DPMS on.
- **A crashed connector can leave the monitor connected.** The driver
  disconnects it when a wait request is cancelled (e.g. the connector process
  ended), but there's a short gap between two wait requests where no request is
  pending.
