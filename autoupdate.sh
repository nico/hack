#/bin/bash

# A bash script that realizes when it has changed on disk and then re-execs
# itself.

mtime() {
  case "$OSTYPE" in
    darwin*)  stat -f %m $1 ;;
    linux*)   stat -c %Y $1 ;;
    *)        echo "unknown: $OSTYPE"; exit 1 ;;
  esac
}

SELFTIME=$(mtime $0)

while true; do
  sleep 2
  NEW_SELFTIME=$(mtime $0)
  if [ $SELFTIME != $NEW_SELFTIME ]; then
    date && echo changed
    exec $0
  fi
done
