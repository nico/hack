#!/bin/bash
if [ -e syncbot.state ]; then
  build_num=$(cat syncbot.state)
else
  build_num=1
fi

# Make created files world-readable.
umask 022

while true; do
  SECONDS=0
  if [[ "$OSTYPE" == "msys" ]]; then
    # The `tee` and the `/usr/bin/env python` both
    # seem to not work well in `git bash`.
    python syncbot.py >curbuild.txt 2>&1
    EXIT_CODE=$?
  else
    ./syncbot.py 2>&1 | tee curbuild.txt
    EXIT_CODE=${PIPESTATUS[0]}
  fi
  echo '{ "elapsed_s":' $SECONDS', "exit_code":' $EXIT_CODE '}' \
      > curbuild.meta.json

  # Reuse build numbers of no-op builds.
  if ! grep -q 'no new commits. sleeping for 30s' curbuild.txt; then
    mv curbuild.txt buildlogs/$build_num.txt
    mv curbuild.meta.json buildlogs/$build_num.meta.json
    ((build_num++))
    echo $build_num > syncbot.state
  fi

  # Upload buildlogs.
  case "$OSTYPE" in
    darwin*) rsync -az buildlogs/ llvm@45.33.8.238:~/buildlog/mac ;;
    linux*)  rsync -az buildlogs/ llvm@45.33.8.238:~/buildlog/linux ;;
    # msys2.org's rsync binary doesn't support old-style --compress / -z
    # due to its external zlib. Use --new-compress / -zz to get compression.
    msys)    rsync -azz buildlogs/ llvm@45.33.8.238:~/buildlog/win ;;
  esac
done
