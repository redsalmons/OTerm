#!/bin/zsh
set -euo pipefail

APP_NAME="OceanTerm"
APP_DISPLAY_NAME="OceanTerm"
VERSION="1.0.3"

ARM64_BIN="build_mac/bin/OceanTerm"
X86_BIN="build_x86/bin/OceanTerm"
BUNDLE_DIR="build_universal"
APP_BUNDLE="$BUNDLE_DIR/${APP_NAME}.app"
DMG_NAME="${APP_NAME}-macos-universal-${VERSION}.dmg"

echo "=== OceanTerm Universal macOS Packaging ==="

rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS" "$APP_BUNDLE/Contents/Resources" "$BUNDLE_DIR/dmg_staging"

# ---------- Info.plist ----------
cat > "$APP_BUNDLE/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key><string>com.oceanterm.app</string>
    <key>CFBundleName</key><string>${APP_DISPLAY_NAME}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>CFBundleIconFile</key><string>OceanTerm.icns</string>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSMinimumSystemVersion</key><string>10.15</string>
</dict>
</plist>
EOF

# ---------- universal executable ----------
echo "Creating universal executable..."
lipo -create "$ARM64_BIN" "$X86_BIN" -output "$APP_BUNDLE/Contents/MacOS/$APP_NAME"

# ---------- dependency collection ----------
# Dep names differ per arch (Homebrew arm64 wx references -3.3 / -3.3.2.0.0
# names while source-built x86_64 wx uses -3.3.2). For each arch we resolve
# every dep to its REAL file; the canonical bundled name is that real file's
# basename, so dep names that resolve to the same real file collapse into a
# single bundled copy — two differently-named copies of the same library would
# load duplicate ObjC classes and break the event/paint dispatch.

is_system() { case "$1" in /System/*|/usr/lib/*|@executable_path/*|@loader_path/*) return 0 ;; esac; return 1; }

# collect_deps <root binary> <extra @rpath search dir> → "depName<TAB>realPath"
collect_deps() {
    local bin="$1" rpathdir="$2"
    local tmpfile; tmpfile=$(mktemp)
    local queue=("$bin") seen=()
    local cur="" dir="" name="" real="" mark=""
    while [ ${#queue[@]} -gt 0 ]; do
        cur="${queue[1]}"; queue=("${(@)queue[2,-1]}")
        dir="$(dirname "$cur")"
        while IFS= read -r dep; do
            is_system "$dep" && continue
            name="$(basename -- "$dep")"
            name="${name//\"/}"
            case "$name" in *.dylib) ;; *) continue ;; esac
            [ -z "$name" ] && continue
            [ "$name" = "$(basename "$bin")" ] && continue
            case "$dep" in
                @rpath/*|@loader_path/*)
                    if [ -e "$dir/$name" ]; then real="$(readlink -f "$dir/$name")"
                    elif [ -e "$rpathdir/$name" ]; then real="$(readlink -f "$rpathdir/$name")"
                    elif [ -e "$(dirname "$bin")/$name" ]; then real="$(readlink -f "$(dirname "$bin")/$name")"
                    else continue; fi ;;
                *)
                    [ -e "$dep" ] || { echo "WARN: dep missing: $dep" >&2; continue; }
                    real="$(readlink -f "$dep")" ;;
            esac
            mark="${name}	${real}"
            case " ${seen[*]} " in *" ${mark} "*) continue ;; esac
            seen+=("$mark")
            printf '%s\n' "$mark" >> "$tmpfile"
            queue+=("$real")
        done < <(otool -L "$cur" | awk 'NR>1 {gsub(/\(.*/,""); print $1}')
    done
    cat "$tmpfile"; rm -f "$tmpfile"
}

typeset -A ARM_CANON X86_CANON ARM_REAL X86_REAL NEED_ARM NEED_X86

while IFS=$'\t' read -r n r; do
    n="${n//\"/}"; r="${r//\"/}"
    [ -n "$n" ] && [ -n "$r" ] || continue
    c="$(basename -- "$r")"; c="${c//\"/}"
    ARM_CANON[$n]="$c"; ARM_REAL[$n]="$r"; NEED_ARM[$c]="$r"
done < <(collect_deps "$ARM64_BIN" "/opt/homebrew/lib")

while IFS=$'\t' read -r n r; do
    n="${n//\"/}"; r="${r//\"/}"
    [ -n "$n" ] && [ -n "$r" ] || continue
    c="$(basename -- "$r")"; c="${c//\"/}"
    X86_CANON[$n]="$c"; X86_REAL[$n]="$r"; NEED_X86[$c]="$r"
done < <(collect_deps "$X86_BIN" "$HOME/oterm-deps/x86_64/lib")

typeset -A ALL_CANON
for n in ${(k)NEED_ARM}; do ALL_CANON[$n]=1; done
for n in ${(k)NEED_X86}; do ALL_CANON[$n]=1; done

echo "arm64 deps: ${#ARM_CANON}, x86_64 deps: ${#X86_CANON}, bundled files: ${#ALL_CANON}"

# ---------- bundle libs ----------
echo "Bundling dylibs..."
RES="$APP_BUNDLE/Contents/Resources"
for canon in ${(k)ALL_CANON}; do
    canon="${canon//\"/}"
    case "$canon" in *.dylib) ;; *) continue ;; esac
    [ -n "$canon" ] || continue
    a="${NEED_ARM[$canon]-}"; x="${NEED_X86[$canon]-}"
    [ -n "$a" ] || [ -n "$x" ] || { echo "ERROR: no source dylib for $canon" >&2; exit 1; }
    if [ -n "$a" ] && [ -n "$x" ]; then
        lipo -create "$x" "$a" -output "$RES/$canon"
    elif [ -n "$a" ]; then
        cp "$a" "$RES/$canon"
    else
        cp "$x" "$RES/$canon"
    fi
    install_name_tool -id "@rpath/$canon" "$RES/$canon" 2>/dev/null || true
done

# ---------- rewrite references ----------
# Map each original dep path to @rpath/<canonical>. Applies across slices:
# an arm64-only old name only matches in the arm64 slice, and vice versa.
rewrite() {
    local file="$1"
    otool -L "$file" | awk 'NR>1 {gsub(/\(.*/,""); print $1}' | sort -u | while read -r dep; do
        is_system "$dep" && continue
        local b="${dep##*/}"
        local canon="${ARM_CANON[$b]-}"
        [ -z "$canon" ] && canon="${X86_CANON[$b]-}"
        [ -n "$canon" ] || continue
        [ "$dep" = "@rpath/$canon" ] && continue
        install_name_tool -change "$dep" "@rpath/$canon" "$file" 2>/dev/null || true
    done
    # also normalise any self-name that was used as a dep reference
    local self; self="$(basename "$file")"
    install_name_tool -id "@rpath/$self" "$file" 2>/dev/null || true
}

for f in "$RES"/*.dylib; do rewrite "$f"; done
rewrite "$APP_BUNDLE/Contents/MacOS/$APP_NAME"

# Ensure the bundled dylib directory is on the rpath search list. Drop any
# build-machine absolute rpaths first so missing bundling can't be masked on
# the dev machine by leftover paths.
BIN="$APP_BUNDLE/Contents/MacOS/$APP_NAME"
otool -l "$BIN" | awk '/LC_RPATH/{getline; getline; if ($1=="path") print $2}' | while read -r rp; do
    case "$rp" in @*) ;; *) install_name_tool -delete_rpath "$rp" "$BIN" 2>/dev/null || true ;; esac
done
install_name_tool -add_rpath "@executable_path/../Resources" "$BIN"
otool -l "$BIN" | grep -A3 LC_RPATH | grep '@executable_path/../Resources' >/dev/null \
    || { echo "ERROR: @executable_path/../Resources rpath missing" >&2; exit 1; }

# ---------- extras ----------
if [ -d "locales" ]; then cp -R locales "$APP_BUNDLE/Contents/Resources/" 2>/dev/null || true; fi
if [ -f "OceanTerm.icns" ]; then cp OceanTerm.icns "$APP_BUNDLE/Contents/Resources/"; fi

# ---------- sign ----------
# lipo/install_name_tool leave each bundled dylib with an invalid signature,
# and on arm64 an adhoc-modified dylib gets SIGKILL'd by cs_invalid_page at
# load time. Re-sign every dylib, then the executable, then the bundle.
echo "Signing..."
for f in "$RES"/*.dylib; do
    codesign --force --sign - --timestamp=none "$f" || { echo "ERROR: codesign failed for $f" >&2; exit 1; }
done
codesign --force --sign - --timestamp=none "$APP_BUNDLE/Contents/MacOS/$APP_NAME" || { echo "ERROR: codesign failed for executable" >&2; exit 1; }
codesign --force --deep --sign - --timestamp=none "$APP_BUNDLE" || { echo "ERROR: codesign failed for bundle" >&2; exit 1; }
codesign -v "$APP_BUNDLE" || { echo "ERROR: bundle verification failed" >&2; exit 1; }

# ---------- DMG ----------
echo "Creating DMG..."
rm -f "$BUNDLE_DIR/$DMG_NAME"
rm -rf "$BUNDLE_DIR/dmg_staging"
mkdir -p "$BUNDLE_DIR/dmg_staging"
cp -R "$APP_BUNDLE" "$BUNDLE_DIR/dmg_staging/"
ln -sf /Applications "$BUNDLE_DIR/dmg_staging/Applications"
(cd "$BUNDLE_DIR/dmg_staging" && hdiutil create -volname "${APP_NAME}" -srcfolder . -ov -format UDZO "../$DMG_NAME")

echo ""
echo "=== Done ==="
echo "App bundle: $APP_BUNDLE"
echo "DMG:        $BUNDLE_DIR/$DMG_NAME"
lipo -info "$APP_BUNDLE/Contents/MacOS/$APP_NAME"
echo "dylibs bundled: $(ls "$RES"/*.dylib | wc -l)"
