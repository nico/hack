#/bin/bash

# Use like
# watch.sh ~/buildlog ~/hack > watch.out 2> watch.err < /dev/null
# disown -h

# FIXME maybe add -m flag for robustness
# FIXME maybe add -d flag instead of the disown stuff above

while inotifywait -e create -r $1; do
  # Update annotation script.
  # FIXME: Maybe exec self if the pull updated watch.sh?
  (cd $2; git pull)

  # Create output. Note that this is in the watched directory,
  # so it's important that inotifywait only looks for create, not for modify.
  $2/llvmgnsyncbot/annotate.py $1 > $1/summary.html
done
