#!/usr/bin/env bash
# ===========================================================================
#  Open New Vegas - Linux launcher
#
#  Run this to play:   ./play.sh
#  It:
#    1. Finds the `walker` binary next to this script.
#    2. Auto-detects your Fallout: New Vegas install (native Steam or Proton)
#       and exports ONV_FNV_PATH, which the engine reads.
#    3. Launches the walker.
#
#  ONV_FNV_PATH must point at the install ROOT - the directory that CONTAINS
#  the "Data" subfolder (case-insensitive on real installs).
#
#  If the game isn't found we still launch: the walker falls back to
#  procedural Mojave terrain, and we print how to set ONV_FNV_PATH by hand.
# ===========================================================================
set -u

# --- Locate this script's directory and the walker binary -----------------
# Resolve symlinks so the launcher works from anywhere.
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  DIR="$(cd -P "$(dirname "$SOURCE")" >/dev/null 2>&1 && pwd)"
  SOURCE="$(readlink "$SOURCE")"
  [[ "$SOURCE" != /* ]] && SOURCE="$DIR/$SOURCE"
done
HERE="$(cd -P "$(dirname "$SOURCE")" >/dev/null 2>&1 && pwd)"

WALKER="$HERE/walker"
[ -x "$WALKER" ] || WALKER="$HERE/walker"  # keep name explicit

if [ ! -f "$WALKER" ]; then
  echo "[ERROR] 'walker' was not found next to this launcher."
  echo "        Expected: $WALKER"
  echo "        Make sure you extracted the whole archive and run play.sh from inside it."
  exit 1
fi
chmod +x "$WALKER" 2>/dev/null || true

# --- has_data <root>: true if <root>/Data (any case) exists ---------------
has_data() {
  local root="$1"
  [ -d "$root/Data" ] || [ -d "$root/data" ] || [ -d "$root/DATA" ]
}

# --- Respect an existing, valid ONV_FNV_PATH ------------------------------
GAME=""
if [ -n "${ONV_FNV_PATH:-}" ] && has_data "$ONV_FNV_PATH"; then
  echo "[info] Using ONV_FNV_PATH from your environment:"
  echo "       $ONV_FNV_PATH"
  GAME="$ONV_FNV_PATH"
fi

# --- Candidate Steam library roots ----------------------------------------
# The game's exterior name under common/ is "Fallout New Vegas".
GAME_DIR_NAME="Fallout New Vegas"

if [ -z "$GAME" ]; then
  # Common Steam roots (native + flatpak). compatdata is where Proton runs it,
  # but the actual install files still live under common/.
  STEAM_ROOTS=(
    "$HOME/.steam/steam"
    "$HOME/.steam/root"
    "$HOME/.local/share/Steam"
    "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"
    "/usr/local/games/Steam"
  )

  # Try the default library under each root.
  for root in "${STEAM_ROOTS[@]}"; do
    cand="$root/steamapps/common/$GAME_DIR_NAME"
    if has_data "$cand"; then GAME="$cand"; break; fi
  done
fi

# --- Parse libraryfolders.vdf for extra (non-default) library locations ---
if [ -z "$GAME" ]; then
  for root in \
    "$HOME/.steam/steam" \
    "$HOME/.local/share/Steam" \
    "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
    vdf="$root/steamapps/libraryfolders.vdf"
    [ -f "$vdf" ] || continue
    # Extract the quoted path on each `"path"  "..."` line.
    while IFS= read -r libpath; do
      [ -n "$libpath" ] || continue
      cand="$libpath/steamapps/common/$GAME_DIR_NAME"
      if has_data "$cand"; then GAME="$cand"; break; fi
    done < <(grep -i '"path"' "$vdf" | sed -E 's/.*"path"[[:space:]]*"([^"]+)".*/\1/')
    [ -n "$GAME" ] && break
  done
fi

# --- Export or fall back ----------------------------------------------------
if [ -n "$GAME" ]; then
  export ONV_FNV_PATH="$GAME"
  echo "[info] Found Fallout: New Vegas:"
  echo "       $ONV_FNV_PATH"
else
  echo ""
  echo "[info] Could not auto-detect your Fallout: New Vegas install."
  echo "       Launching with PROCEDURAL Mojave terrain (no game files needed)."
  echo ""
  echo "       To walk the REAL terrain, set ONV_FNV_PATH to your install root -"
  echo "       the directory that contains the \"Data\" subfolder, e.g.:"
  echo ""
  echo "         export ONV_FNV_PATH=\"\$HOME/.steam/steam/steamapps/common/Fallout New Vegas\""
  echo ""
  echo "       Then run ./play.sh again."
  echo ""
fi

# --- Launch -----------------------------------------------------------------
echo "[info] Starting Open New Vegas..."
echo ""
cd "$HERE" || exit 1
"$WALKER"
RC=$?

if [ "$RC" -ne 0 ]; then
  echo ""
  echo "[warn] walker exited with code $RC."
fi
exit "$RC"
