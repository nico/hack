#!/bin/bash

# Use like
# hack/llvmgnsyncbot/watch.sh buildlog html hack > html/watch_out.txt 2> html/watch_err.txt < /dev/null &
# disown -h

# FIXME maybe add -d flag instead of the disown stuff above

inotifywait -m -e create -r $1 | while read; do
  while read -t 0; do read; done  # Drain additional queued-up events, if any.

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
