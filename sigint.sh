#!/bin/bash

# sigint.py is a python program that just sleeps for a while.
# sigint.sh is a shell script that calls it in a loop.

# By default, hitting ctrl-c in this script will kill the python program,
# but the shell loop will call it right again. If `sleep 10`
# (or `/bin/sleep 10`) is called instead of sigint.py, ctrl-c interrupts
# both the sleep and the loop.

# https://www.cons.org/cracauer/sigint.html is a good explanation of why.
# In short, bash postpones ctrl-c handling until children are done, and then
# only self-aborts if the child didn't abort itself with something that has
# the same behavior as the default SIGINT handler. And Python apparently
# installs a SIGINT handler that doesn't have that behavior -- instead,
# it raises a KeyboardInterrupt and it's up to each script to handle that
# correctly.

# There are two possible fixes:
# 1. Change the Python program to make KeyboardInterrupt abort the process
#    as if the (non-Python) default SIGINT handler had run.
# 2. Change the outer shell script to explicitly quit on sigint(). Bash
#    will call the script's trap handler only after the inner script has quit.
#    That would look like so:
#        sigint() {
#          echo sigint bash
#          exit
#        }
#        trap sigint INT
#    Note that if this script was called inside yet another script, it really
#    should do
#          trap INT
#          kill -INT $$
#    instead of calling `exit`, for the same reason as above.


while true; do
  python sigint.py
done
