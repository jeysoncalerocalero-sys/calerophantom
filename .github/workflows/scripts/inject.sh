#!/usr/bin/env bash
set -euo pipefail

IN="$1"; SO="$2"; OUT="${3:-PHANTOM_FF.apk}"; KS="${4:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BAKSMALI="$ROOT/tools/baksmali.jar"
SMALI="$ROOT/tools/smali.jar"

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/base"

if unzip -l "$IN" | grep -qE 'base\.apk'; then
  unzip -q "$IN" -d "$W/x"
  BASE="$W/x/base.apk"
  SPLIT="$(ls "$W"/x/*arm64*v8a*.apk 2>/dev/null | head -n1 || true)"
else
  BASE="$IN"; SPLIT=""
fi

unzip -q "$BASE" -d "$W/base"
cd "$W/base"

if [ -n "$SPLIT" ]; then
  unzip -q -o "$SPLIT" -d "$W/s"
  [ -d "$W/s/lib" ] && cp -r "$W/s/lib" .
fi

mkdir -p lib/arm64-v8a
cp "$SO" lib/arm64-v8a/libphantom.so

if [ -f "$ROOT/icon.png" ]; then
  n=0
  while IFS= read -r f; do
    cp "$ROOT/icon.png" "$f"; n=$((n+1))
  done < <(find res -type f \( -iname "*ic_launcher*" -o -iname "*app_icon*" \
           -o -iname "*icon*.png" -o -iname "*icon*.webp" -o -iname "*logo*.png" \) 2>/dev/null || true)
  echo "[*] $n iconos cambiados"
fi

TARGET=""
for d in classes*.dex; do
  if java -jar "$BAKSMALI" list classes "$d" 2>/dev/null \
     | grep -qx "Lcom/dts/freefireth/FFMainActivity;"; then TARGET="$d"; break; fi
done
[ -z "$TARGET" ] && { echo "no encontre FFMainActivity"; exit 1; }

java -jar "$BAKSMALI" d "$TARGET" -o sm
F="sm/com/dts/freefireth/FFMainActivity.smali"

awk '
  /^\.method/ { inm = ($0 ~ /onCreate/) }
  inm && /^\.locals/ {
      n = substr($2,2)+0
      if (n < 1) print ".locals 1"; else print
      print "    const-string v0, \"phantom\""
      print "    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V"
      inm = 0; next
  }
  { print }
' "$F" > "$F.new"
mv "$F.new" "$F"

java -jar "$SMALI" a sm -o "$TARGET"
rm -rf sm

rm -rf META-INF
zip -qr9 "$W/u.apk" . -x "lib/*" -x "resources.arsc"
zip -q0r "$W/u.apk" lib resources.arsc
zipalign -p -f 4 "$W/u.apk" "$W/a.apk"

if [ -n "$KS" ]; then
  apksigner sign --ks "$KS" --ks-pass pass:android --out "$OUT" "$W/a.apk"
else
  cp "$W/a.apk" "$OUT"
fi
echo "[OK] $OUT"
