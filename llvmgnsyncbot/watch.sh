#/bin/bash

# Use like
# hack/llvmgnsyncbot/watch.sh buildlog html hack > html/watch_out.txt 2> html/watch_err.txt < /dev/null &
# disown -h

# FIXME maybe add -m flag for robustness
# FIXME maybe add -d flag instead of the disown stuff above

while inotifywait -e create -r $1; do
  now=$(TZ=UTC date +%FT%TZ)
  echo $now

  # Update annotation script.
  # FIXME: Maybe exec self if the pull updated watch.sh?
  (cd $3; git pull)

  # Create output.
  SECONDS=0
  $3/llvmgnsyncbot/annotate.py $1 $2
  echo $SECONDS > $2/watchtime.txt
  echo $now $SECONDS >> $2/watchtimes.txt
done
