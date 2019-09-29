#/bin/bash

# Use like
# hack/llvmgnsyncbot/watch.sh buildlog html hack > watch.out 2> watch.err < /dev/null
# disown -h

# FIXME maybe add -m flag for robustness
# FIXME maybe add -d flag instead of the disown stuff above

while inotifywait -e create -r $1; do
  # Update annotation script.
  # FIXME: Maybe exec self if the pull updated watch.sh?
  (cd $3; git pull)

  # Create output.
  $3/llvmgnsyncbot/annotate.py $1 $2
done
