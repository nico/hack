#!/bin/bash

if [ -e syncbot.state ]; then
  build_num=$(cat syncbot.state)
else
  build_num=1
fi

if [[ -e buildlogs/$build_num.txt ]]; then
  echo "got build_num $build_num, but output for that already exists." 2>&1
  echo "exiting." 2>&1
  exit 1
fi

# Make created files world-readable.
umask 022

THIS_DIR=$(dirname "$0")

while true; do
  last_exit=
  prev_log=buildlogs/$((build_num-1)).meta.json
  if [[ -e $prev_log ]]; then
    last_exit=--last-exit=$(sed -E 's/.*"exit_code": ([0-9]+).*/\1/' $prev_log)
  fi

  SECONDS=0
  if [[ "$OSTYPE" == "msys" ]]; then
    # The `tee` and the `/usr/bin/env python` both
    # seem to not work well in `git bash`.
    python $THIS_DIR/syncbot.py $last_exit >curbuild.txt 2>&1
    EXIT_CODE=$?
  else
    $THIS_DIR/syncbot.py $last_exit 2>&1 | tee curbuild.txt
    EXIT_CODE=${PIPESTATUS[0]}
  fi
  echo '{ "elapsed_s":' $SECONDS', "exit_code":' $EXIT_CODE '}' \
      > curbuild.meta.json

  # Reuse build numbers of no-op builds.
  if ! grep -q 'no new commits. sleeping for 30s' curbuild.txt; then
    mkdir -p buildlogs
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
