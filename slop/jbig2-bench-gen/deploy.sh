#!/bin/bash
# Deploys report.html and referenced images to a separate GitHub Pages repo.
# Usage: ./deploy.sh
#
# Setup:
#   1. Create a GitHub repo for the static site (e.g. <user>/jbig2report)
#   2. Enable GitHub Pages on its main branch in repo settings
#   3. Pass the clone URL as an argument
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: ./deploy.sh <deploy-repo-url>"
  echo "Example: ./deploy.sh git@github.com:<user>/jbig2report.git"
  exit 1
fi

DEPLOY_REPO="$1"

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -f "$REPO_DIR/report.html" ]; then
  echo "Error: report.html not found. Run 'uv run run_tests.py' first."
  exit 1
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

git clone --quiet --depth 1 "$DEPLOY_REPO" "$TMPDIR/site" 2>/dev/null \
  || git init --quiet "$TMPDIR/site"

cd "$TMPDIR/site"

# If freshly init'd, set up remote
if ! git remote get-url origin >/dev/null 2>&1; then
  git remote add origin "$DEPLOY_REPO"
fi

# Preserve README.md if it exists
if [ -f README.md ]; then
  cp README.md "$TMPDIR/README.md.bak"
fi

# Clear old content
git rm -rf --quiet . 2>/dev/null || true
rm -rf ./*

# Restore README.md
if [ -f "$TMPDIR/README.md.bak" ]; then
  cp "$TMPDIR/README.md.bak" README.md
fi

# Copy report, source files, and images
cp "$REPO_DIR/report.html" index.html
mkdir -p build
cp -r "$REPO_DIR/build/jbig2" build/jbig2
cp -r "$REPO_DIR/build/pdfs" build/pdfs
cp "$REPO_DIR"/build/*.zip build/
mkdir -p build/output build/diffs
for d in "$REPO_DIR"/build/output/*/; do
  cp -r "${d%/}" build/output/
done
for d in "$REPO_DIR"/build/diffs/*/; do
  cp -r "${d%/}" build/diffs/
done

# Commit and push
git add -A
if git diff --cached --quiet; then
  echo "No changes to deploy."
else
  git commit --quiet -m "Update report $(date +%Y-%m-%d)"
  git push --force origin HEAD:main
  echo "Deployed to $DEPLOY_REPO"
fi
