#!/bin/bash
if [ -e syncbot.state ]; then
  build_num=$(cat syncbot.state)
else
  build_num=1
fi

# Make created files world-readable.
umask 022

# Add something like 
# Alias /syncbot "/usr/local/google/home/thakis/src/llvm-project/buildlogs/"  
# <Directory /usr/local/google/home/thakis/src/llvm-project/buildlogs/>
#    Options Indexes
#    AllowOverride None
#    Require all granted
# </Directory>
# to /etc/apache2/apache2.conf to map buildlogs to dotc.c.googlers.com/syncbot
# Check /var/log/apache2/error.log for errors (in particular, all dirs on
# that path need a+x permissions).

while true; do
  # XXX interesting state to return:
  # - build successful or not
  # - tests successful or not
  # - were there any new commits at all
  # - does stuff need to be merged
  # - did the script commit something
  # - duration of build and each test (?)
  # - revisions new in this build
  # - number of build edges run (?)
  SECONDS=0
  ./syncbot.py 2>&1 | tee buildlogs/$build_num.txt
  echo '{ "elapsed_s":' $SECONDS', "exit_code":' ${PIPESTATUS[0]} '}' \
      > buildlogs/$build_num.meta.json

  # Reuse build numbers of no-op builds.
  if ! grep -q 'no new commits. sleeping for 30s' buildlogs/$build_num.txt; then
    ((build_num++))
    echo $build_num > syncbot.state
  fi

  case "$OSTYPE" in
    darwin*) rsync -az buildlogs/ llvm@45.33.8.238:~/buildlog/mac ;;
    linux*)  rsync -az buildlogs/ llvm@45.33.8.238:~/buildlog/linux ;;
  esac
done
