#!/bin/bash

# This is a shell script that calls in a loop a python program that just sleeps
# for a while.

# The naive implementation would look like so:

#     while true; do python -c 'import time; time.sleep(10)'; done

# But that way, hitting ctrl-c in this script will interrupt only the python
# program, but the shell loop continue and call it right again, and the script
# isn't easily ctrl-c-able.

# If `sleep 10` (or `/bin/sleep 10`) is called instead of python, ctrl-c
# interrupts both the sleep and the loop, like you'd expect.

# Why is that? Ctrl-c sends SIGINT to all processes in the process group
# (i.e. both the bash interpreter running this script, and either python
# or sleep).

# Bash's interrupt handler postpones SIGINT handling until its children
# (python or time) are done, and then only self-aborts if the child didn't
# abort itself with something that has the same behavior as the default SIGINT
# handler. time has a default SIGINT handler, but Python 
# installs a SIGINT handler that doesn't have that behavior, so that
# it can raise a KeyboardInterrupt exception. And for some reason, a
# KeyboardInterrupt leaking out of the program isn't handled by the interpreter
# like the default SIGINT handler, it's up to each python program to catch
# the exception and call the default SIGINT handler. Without that,
# Python programs can't be ctrl-c'd in a nice way.

# (Every other program that handles SIGINT needs to do this too, but Python
# does the wrong thing by default. The "normal" default SIGINT handler
# does the right thing.)

# https://www.cons.org/cracauer/sigint.html has more words on this topic.

# There are two possible fixes:
# 1. Change the Python program to make KeyboardInterrupt abort the process
#    as if the (non-Python) default SIGINT handler had run.
# 2. Change the outer shell script to explicitly quit on sigint(). Bash
#    will call the script's trap handler only after the inner script has quit.
#    That would look like so:
#        sigint() {
#          echo sigint bash
#          trap - INT  # Restore default SIGINT handler.
#          kill -INT $$  # Call default SIGINT handler.
#        }
#        trap sigint INT
#    Note that the signal handler uses the same "restore default INT handler
#    and send SIGINT to self to exit" logic (instead of calling `exit`).
#    This is for the same reason as above: if this script was called inside
#    another script.

while true; do
  python -c "$(cat <<-'ENDPYTHON'
	#!/usr/bin/env python
	from __future__ import print_function
	import os, signal, time

	def main():
	    time.sleep(10)

	if __name__ == '__main__':
	    try:
	        main()
	    except KeyboardInterrupt:
	        print('sigint python')
	        # See https://www.cons.org/cracauer/sigint.html
	        # (Alternatively, use signal(SIGINT, SIG_DFL) at program start,
	        # but make sure to undo that if the returned handler isn't
	        # default_int_handler, which it isn't e.g. under nohup.
	        # Since we did get KeyboardInterrupt, we know the old handler
	        # is default_int_handler here.)
	        s = signal.signal(signal.SIGINT, signal.SIG_DFL)
	        assert s == signal.default_int_handler
	        os.kill(os.getpid(), signal.SIGINT)
	ENDPYTHON
  )"
done
