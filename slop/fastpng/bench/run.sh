#!/bin/bash
# Reproduces the README's numbers: builds png-bench if needed, downloads the images listed in
# images.txt into build/images/ (once; each checked against its SHA-256), and runs the benchmark on
# them as written, re-encoded by fpng, and fed in pieces to the decoders that can resume. Set
# PNGSUITE to a PngSuite directory to also run --check.
#   bench/run.sh [png-bench options, e.g. --only fastpng,png-rs]
set -e
cd "$(dirname "$0")"
[ -x build/png-bench ] || ./build.sh

# Wikimedia asks for an identifying User-Agent, and for not hurrying.
USER_AGENT="fastpng-bench/1.0 (https://github.com/nico/hack/tree/main/slop/fastpng)"
hash_of() { shasum -a 256 "$1" 2>/dev/null | cut -d' ' -f1; }

mkdir -p build/images
files=()
failed=0
while IFS=$'\t' read -r name source bytes kind sha what; do
    case "$name" in '#'* | '') continue ;; esac
    dst="build/images/$name"
    files+=("$dst")
    [ "$(hash_of "$dst")" = "$sha" ] && continue
    # gitiles serves raw files base64-encoded.
    case "$source" in
    *'?format=TEXT') curl -sfL -A "$USER_AGENT" "$source" | base64 -d > "$dst.part" ;;
    *) curl -sfL -A "$USER_AGENT" -o "$dst.part" "$source" ;;
    esac || { rm -f "$dst.part"; echo "can't download $name from $source" >&2; failed=1; continue; }
    mv "$dst.part" "$dst"
    [ "$(hash_of "$dst")" = "$sha" ] || echo "note: $name changed at its source; numbers will differ ($kind)" >&2
    sleep 1
done < images.txt
[ $failed = 0 ] || exit 1

# Every decode gets freshly allocated output. macOS's malloc keeps some freed large blocks mapped
# for reuse, which makes a big image's time depend on what earlier decodes left behind; without
# that cache, each decode faults its output in, whichever decoder and file came before.
export MallocLargeCache=0

if [ -n "$PNGSUITE" ]; then
    ./build/png-bench --check "$PNGSUITE"/*.png "${files[@]}"
fi
echo "== as written"
./build/png-bench -n 9 "$@" "${files[@]}"
echo "== re-encoded by fpng"
./build/png-bench -n 9 --fpng-reencode "$@" "${files[@]}"
echo "== decoded as the file arrives, in 64 pieces"
./build/png-bench -n 9 --incremental 64 "$@" "${files[@]}"
echo "== what each shows a quarter of the way through"
./build/png-bench --incremental 8 --preview 2 "$@" "${files[@]}"
