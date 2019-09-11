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
  ./syncbot.py 2>&1 | tee buildlogs/$build_num.txt

  # Reuse build numbers of no-op builds.
  if ! grep -q 'no new commits. sleeping for 30s' buildlogs/$build_num.txt; then
    ((build_num++))
    echo $build_num > syncbot.state
  fi
done

