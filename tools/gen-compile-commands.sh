#!/usr/bin/env bash
#
# Generates compile_commands.json for the whole repo, so that clangd /
# VSCode IntelliSense sees the same flags the real builds use.
#
# Flags are queried from pkg-config and psp-config at run time rather than
# hardcoded, so they follow toolchain and library upgrades. Re-run this after
# adding a source file or changing build flags.
#
# Usage: tools/gen-compile-commands.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/compile_commands.json"

# JSON string escaping: backslash and double quote.
json_escape() {
  local s=$1
  s=${s//\\/\\\\}
  s=${s//\"/\\\"}
  printf '%s' "$s"
}

entries=()

# add_entry <directory> <file> <arg>...
add_entry() {
  local dir=$1 file=$2
  shift 2
  local args=""
  local a
  for a in "$@"; do
    [[ -n $args ]] && args+=", "
    args+="\"$(json_escape "$a")\""
  done
  entries+=("$(printf '  {\n    "directory": "%s",\n    "file": "%s",\n    "arguments": [%s]\n  }' \
    "$(json_escape "$dir")" "$(json_escape "$file")" "$args")")
}

# ---------------------------------------------------------------- desktop ---
# The desktop (src/desktop), built by the root Makefile. Its flags come from
# that Makefile (CFLAGS, DESKTOP_TD_CPPFLAGS and DESKTOP_DAEMON_CPPFLAGS,
# printed through `make --eval` without a target of its own), so the two can
# never drift apart. GTK 4 is added for the UI, which the Makefile doesn't
# build yet.
#
# The Windows-only sources are skipped here, they're in the windows section.
DESKTOP_DIR="$ROOT/src/desktop"

if desktop_cflags=$(make -s -C "$ROOT" --eval 'print-desktop-cflags: ; @echo $(CFLAGS) $(DESKTOP_TD_CPPFLAGS) $(DESKTOP_DAEMON_CPPFLAGS)' print-desktop-cflags 2>/dev/null); then
  read -r -a DESKTOP_CFLAGS <<<"$desktop_cflags"
  read -r -a GTK_CFLAGS <<<"$(pkg-config --cflags gtk4 2>/dev/null || true)"

  while IFS= read -r -d '' src; do
    add_entry "$ROOT" "$src" \
      "${CC:-gcc}" "${DESKTOP_CFLAGS[@]}" "${GTK_CFLAGS[@]}" -c "$src"
  done < <(find "$DESKTOP_DIR" -name '*.c' \
             -not -path '*/display/connector/iddcx*' -not -name 'iddcx*' -not -name 'wddm.c' -not -name 'xpdm.c' -not -name 'wasapi.c' -not -name 'sendinput.c' -not -path '*/daemon/system/windows.c' \
             -print0 2>/dev/null | sort -z)
else
  echo "warning: couldn't read the desktop flags from the root Makefile, skipping src/desktop" >&2
fi

# -------------------------------------------------------------------- psp ---
# Mirrors what $(PSPSDK)/lib/build.mak actually runs; verified with
# 'make -n main.o' in psp/source.
if command -v psp-config >/dev/null 2>&1; then
  PSPSDK="$(psp-config --pspsdk-path)"
  PSPDEV_PSP="$(psp-config --psp-prefix)"

  psp_flags=(
    -I"$PSPDEV_PSP/include" -I"$PSPSDK/include"
    -O2 -G0 -Wall -std=gnu23 -fcommon -D_PSP_FW_VERSION=371
  )

  # New tree (src/psp), once it exists.
  while IFS= read -r -d '' src; do
    add_entry "$ROOT" "$src" \
      psp-gcc -I"$ROOT/src" "${psp_flags[@]}" -c "$src"
  done < <(find "$ROOT/src/psp" -name '*.c' -print0 2>/dev/null | sort -z)

  # Legacy tree (psp/source). Its Makefile compiles with -I. from that
  # directory, so "directory" must be psp/source for #include "com.h" to
  # resolve.
  while IFS= read -r -d '' src; do
    add_entry "$ROOT/psp/source" "$src" \
      psp-gcc -I. "${psp_flags[@]}" -c "$src"
  done < <(find "$ROOT/psp/source" -name '*.c' -not -path '*/libjpeg/*' -print0 2>/dev/null | sort -z)
else
  echo "warning: psp-config not on PATH, skipping PSP sources" >&2
fi

# ---------------------------------------------------------------- windows ---
# Windows-only sources: the display and audio connectors compiled on Windows, and the IddCx
# driver. They're parsed by the MinGW cross-compiler, so IntelliSense finds
# <windows.h> and defines _WIN32, instead of parsing them as Linux code.
#
# GLib comes from MinGW's pkg-config when a Windows build of it is installed,
# otherwise from the host one: not the right build, but good enough to parse.
#
# The driver also needs the WDK headers (wdf.h, wudfwdm.h, iddcx.h), which
# only ship on Windows: copy them over and list their folders, separated by
# ':', in WDK_INCLUDE (e.g. their um/, shared/, wdf/umdf/2.x/ and iddcx/1.x/).
MINGW_CC=x86_64-w64-mingw32-gcc

if command -v "$MINGW_CC" >/dev/null 2>&1; then
  if command -v x86_64-w64-mingw32-pkg-config >/dev/null 2>&1; then
    read -r -a WINDOWS_GLIB_CFLAGS <<<"$(x86_64-w64-mingw32-pkg-config --cflags glib-2.0 gio-2.0)"
  else
    read -r -a WINDOWS_GLIB_CFLAGS <<<"$(pkg-config --cflags glib-2.0 gio-2.0 2>/dev/null || true)"
    echo "note: no Windows GLib (x86_64-w64-mingw32-pkg-config), using the host GLib headers for the Windows connectors" >&2
  fi

  windows_flags=(-std=gnu23 -Wall -Wextra -Wno-pragma-once-outside-header)

  for src in "$ROOT"/src/desktop/display/connector/{iddcx,wddm,xpdm}.c "$ROOT"/src/desktop/audio/connector/wasapi.c "$ROOT"/src/desktop/controller/input/sendinput.c "$ROOT"/src/desktop/daemon/system/windows.c "$ROOT"/src/desktop/iddcx_*.c; do
    [[ -f $src ]] || continue
    add_entry "$ROOT" "$src" \
      "$MINGW_CC" "${windows_flags[@]}" "${WINDOWS_GLIB_CFLAGS[@]}" -c "$src"
  done

  # The driver is built by MSVC as C17, without GLib.
  wdk_flags=()
  if [[ -n ${WDK_INCLUDE:-} ]]; then
    IFS=: read -r -a wdk_dirs <<<"$WDK_INCLUDE"
    for dir in "${wdk_dirs[@]}"; do wdk_flags+=(-I"$dir"); done
  else
    echo "note: WDK_INCLUDE is not set, the IddCx driver includes of the WDK (wdf.h, wudfwdm.h, iddcx.h) stay unresolved" >&2
  fi

  driver="$ROOT/src/desktop/display/connector/iddcx/driver.c"
  if [[ -f $driver ]]; then
    add_entry "$ROOT/src/desktop/display/connector/iddcx" "$driver" \
      "$MINGW_CC" -std=c17 -Wall -Wextra "${wdk_flags[@]}" -c "$driver"
  fi
else
  echo "warning: $MINGW_CC not on PATH, skipping the Windows sources" >&2
fi

# ------------------------------------------------------------------ write ---
if [[ ${#entries[@]} -eq 0 ]]; then
  echo "error: no sources found, refusing to write an empty $OUT" >&2
  exit 1
fi

{
  echo "["
  printf '%s' "${entries[0]}"
  for ((i = 1; i < ${#entries[@]}; i++)); do
    printf ',\n%s' "${entries[i]}"
  done
  printf '\n]\n'
} >"$OUT"

echo "wrote $OUT (${#entries[@]} entries)"
