#!/bin/bash

# Takes a shell command script and runs it on swarming. If the command
# is a shell script, also uploads the script.
# Usage:
#  path/to/swarm-script.sh ls
#  path/to/swarm-script.sh sh -c 'env; pwd'
#  path/to/swarm-script.sh ./my-script.sh

set -e

if [[ $# -lt 1 ]]; then
  echo "usage: $0 cmd [args]"
  exit 1
fi

mkdir -p /tmp/swarm-script
pushd /tmp/swarm-script > /dev/null

PLAT=mac-amd64
# 1. Grab cipd.
if ! [ -x cipd ]; then
  URL=https://chrome-infra-packages.appspot.com/client
  CIPD_VER=git_revision:9f9afb5ef6ef9d4887e8aa2bb617dfdd798f8005
  curl -sSL "$URL?platform=$PLAT&version=$CIPD_VER" -o cipd
  chmod +x cipd
fi

# 2. Use cipd to grap isolate and swarming binaries.
if ! [ -d cipddir ]; then
  ./cipd init cipddir > /dev/null
fi
for TOOL in isolate swarming; do
  TOOLVER=git_revision:56ae79476e3caf14da59d75118408aa778637936 
  if ! [ -x cipddir/$TOOL ]; then
    # Even `-log-level error` doesn't completely shut up cipd install even
    # in the happy case, and even info logging goes to stderr :/
    # So write to an error log file and cat it if the command fails. Sigh.
    if ! ./cipd install infra/tools/luci/$TOOL/$PLAT $TOOLVER \
        -root cipddir -log-level warning > cipd.log 2>&1; then
      cat cipd.log
      exit 1
    fi
  fi
done

popd > /dev/null

# Upload shell script.
if [ "$(which $1)" == "$1" ]; then
  FILE="'$1'"
else
  FILE=
fi
ISO=$(mktemp .swarm-script.isolate.XXXXXX)
trap 'rm -f $ISO' EXIT
echo "{'variables':{'files':[$FILE]}}" > $ISO
HASH=$(/tmp/swarm-script/cipddir/isolate archive -quiet \
    -I isolateserver.appspot.com \
    -i $ISO | grep -o '[a-z0-9]\+')

# Run it.
/tmp/swarm-script/cipddir/swarming trigger \
    -I https://isolateserver.appspot.com \
    -S https://chromium-swarm.appspot.com \
    -d pool=chromium.tests -d os=Mac \
    -s $HASH -raw-cmd -- "$@"
