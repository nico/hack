#!/bin/bash
# A small demo on how to do basic oauth2 with curl,
# and how to do simple goma api calls.

# https://stackoverflow.com/questions/5014823/how-to-profile-a-bash-shell-script-slow-startup
# but with perl instead of %N since macOS date doesn't understand that, and
# with sed's `-u` flag removed for the same reason.
PROFILE=0
if [ "$PROFILE" -eq "1" ]; then
  PS4='+ $(perl -MTime::HiRes=time -e '"'"'printf "%.9f\n", time'"'"')\011 '
  exec 3>&2 2>/tmp/bashstart.$$.log
  set -x
fi

set -ue

if [ -e $HOME/.goma_oauth2_config ]; then
  GOMA_AUTH=$HOME/.goma_oauth2_config
elif [ -e $HTOME/.goma_client_oauth2_config ]; then
  GOMA_AUTH=$HOME/.goma_client_oauth2_config
else
  echo 'Run ~/goma/goma_auth.py login'
  exit 1
fi

# https://www.tldp.org/LDP/abs/html/gotchas.html#BADREAD0
set -- $(jq -r .client_id,.client_secret,.refresh_token $GOMA_AUTH)
client_id=$1
client_secret=$2
refresh_token=$3

# https://stackoverflow.com/questions/53357741/how-to-perform-oauth-2-0-using-the-curl-cli
token_json=$(curl -s --request POST \
  --data client_id=$client_id \
  --data client_secret=$client_secret \
  --data refresh_token=$refresh_token \
  --data grant_type=refresh_token \
https://oauth2.googleapis.com/token)
#https://accounts.google.com/o/oauth2/token)

access_token=$(echo $token_json | jq -r .access_token)

#curl -H "Authorization: Bearer $access_token" \
    #https://oauth2.googleapis.com/tokeninfo

current_url=$(curl -s -H "Authorization: Bearer $access_token" \
  https://clients5.google.com/cxx-compiler-service/downloadurl)
curl -H "Authorization: Bearer $access_token" $current_url/MANIFEST


if [ "$PROFILE" -eq "1" ]; then
  set +x
  exec 2>&3 3>&-
fi
