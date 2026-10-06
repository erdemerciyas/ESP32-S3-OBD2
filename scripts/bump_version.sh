#!/usr/bin/env bash
# Sürümü artırır (version.txt — firmware + Android ortak tek kaynak).
#   scripts/bump_version.sh [patch|minor|major]     (varsayılan: patch)
# Sonra CHANGELOG.md'ye "## vX.Y.Z — tarih — başlık" girdisi ekleyin; commit
# kancası (.githooks/pre-commit) ikisini de denetler, post-commit etiketler.
set -euo pipefail
cd "$(dirname "$0")/.."
part=${1:-patch}
IFS=. read -r ma mi pa < version.txt
case "$part" in
    patch) pa=$((pa + 1)) ;;
    minor) mi=$((mi + 1)); pa=0 ;;
    major) ma=$((ma + 1)); mi=0; pa=0 ;;
    *) echo "kullanım: $0 [patch|minor|major]" >&2; exit 1 ;;
esac
echo "$ma.$mi.$pa" > version.txt
echo "$ma.$mi.$pa"
