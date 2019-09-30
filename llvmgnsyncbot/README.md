LLVM GN Sync Bot
================

This is the code running on the builders and the web server behind
http://45.33.8.238/


Overview
--------

Every builder runs `syncbot.sh`, which in turn runs `syncbot.py` in loop.

`syncbot.py` is responsible for doing a single build, where a build consists
of pulling the newest version of LLVM, building it, and running tests. If
no new changes are available in LLVM's git repository, the script instead
sleeps for a while and then exits.

`syncbot.sh` writes the output of `syncbot.py` to a log file, and if work
was done (i.e. `syncbot.py` did more than just sleep and exit), it `rsync`s
the log file to the buildlog storage server.

The buildlog storage server watches for file creations in the buildlog
directories and runs `annotate.py` to convert the buildlog text files to
html files every time a new log file arrives. The watching is done by the
`watch.sh` script.

The html files are static files and served as such by the web server.

The web server and the buildlog storage server happen to be the same machine
at the moment.

The buildlog server currently pulls the newest version of `annotate.py` before
running it, so landing changes to it takes effect immediately. (`watch.sh`
doesn't currently re-exec itself when it changes since it's so short. Maybe
it should, see `autoupdate.sh` in this repo.)

The builders currently do not pull the newest version of `syncbot.sh/py`, so
changes to it need logging in to the builders and doing something.

Builder setup
-------------

Each builder needs some amount of one-time manual setup.

1. Check out (llvm-project)[https://github.com/llvm/llvm-project/] from github
   to some directory, e.g. `~/src/llvm-project`.

1. Check out this repository here to some other directory.

1. In the root of the llvm repository, symlink `syncbot.sh` to `syncbot.sh` in
   the checkout of this directory, and do the same for `syncbot.py`.

1. Set up the goma client in `~/goma`.

1. Use `out/gn` as build directory, containing this `args.gn`:

       llvm_targets_to_build = "all"
       clang_base_path =
           getenv("HOME") + "/src/chrome/src/third_party/llvm-build/Release+Asserts"
       use_goma = true

1. Create `~/.ssh/id_rsa.pub` if necessary by running `ssh-keygen -t rsa`, then
   copy it to the buildlog server with
   `cat ~/.ssh/id_rsa.pub | ssh llvm@45.33.8.238 'cat >> .ssh/authorized_keys'`

1. On the linux bot, make sure svn auth for https://llvm.org:443 on the linux
   bot is set to the gnsyncbot svn account.

1. In a tmux or screen session, or a CRD window, run `./syncbot.sh` while in
   the LLVM checkout. Then disconnect from the builder.
