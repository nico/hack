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

The html files are static files and are served as such by the web server.

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

1. Check out [llvm-project](https://github.com/llvm/llvm-project/) from github
   to some directory, e.g. `~/src/llvm-project`.

1. Check out this repository here to some other directory, e.g. `~/src/hack`.

1. Set up the goma client somewhere on PATH, so that `goma_ctl` is runnable.
   (For example, by having depot\_tools on PATH.)

1. Use `out/gn` as build directory, containing this `args.gn`:

       llvm_targets_to_build = "all"
       clang_base_path =
           getenv("HOME") + "/src/chrome/src/third_party/llvm-build/Release+Asserts"

       # This should contain the output of `goma_ctl goma_dir`.
       goma_dir = getenv("HOME") + "/src/depot_tools/.cipd_bin"
       use_goma = true

   On Windows, use this for `clang_base_path` and `goma_dir` instead:

       clang_base_path = "C:/src/chrome/src/third_party/llvm-build/Release+Asserts"
       goma_dir = "C:/src/depot_tools/.cipd_bin"

1. On Windows, most of the build runs natively, but rsync needs a cygwin-like
   environment, and since it's needed for that anyways, syncbot.sh runs in it
   too. It's easiest to use a `git bash` shell. To add `rsync` to it, grab
   [rsync off the msys2 repo][rsync] and put it in
   `dirname $(cygpath -w $(which ls))` (just `bin/rsync.exe` is enough).

1. On non-Windows, run `git config core.trustctime false` in the llvm checkout.
   The GN build uses hardlinks to copy files, which changes the ctime on the
   copy source inode. By default, `git checkout -f main` will replace files
   with modified ctime with a new copy of the file, which in turn changes the
   mtime on the copy inputs -- which makes ninja think copy steps are dirty
   and need to be rerun on every build. This setting fixes this.
   [More information][1].

1. Create `~/.ssh/id_rsa.pub` if necessary by running `ssh-keygen -t rsa`, then
   copy it to the buildlog server with
   `cat ~/.ssh/id_rsa.pub | ssh llvm@45.33.8.238 'cat >> .ssh/authorized_keys'`

1. On the linux bot, run `git config user.email llvmgnsyncbot@gmail.com` and
   `git config user.name "LLVM GN Syncbot"` in the LLVM checkout used by the
   bot.

1. On the linux bot, generate a second public key with
   `ssh-keygen -t rsa -f ~/.ssh -f ~/.ssh/llvmgnsyncbot_id_rsa`, and add the
   `.pub` file contents as SSH key on https://github.com/settings/keys for
   the llvmgnsyncbot github account. Run
   `git config core.sshCommand "ssh -i ~/.ssh/llvmgnsyncbot_id_rsa"` in the LLVM
   checkout used by the bot. If the checkout is using https, run
   `git remote set-url origin git@github.com:llvm/llvm-project.git` to move
   to SSH.

1. In a tmux or screen session, or a CRD window, run `../hack/llvmgnsyncbot/syncbot.sh`
   while in the LLVM checkout. Then disconnect from the builder.

1. To automatically start the sync script after reboots:

   On linux, create a file    in your home directory containing this script
   (with paths adjusted), run `chmod +x` on it, and add it to `crontab -e`
   as described in the script:

       #!/bin/bash

       # Put this in your `crontab -e`:
       #
       #     @reboot /home/botusername/syncbotcron.sh

       /bin/sleep 4  # Wait a bit for the network to come up.

       /usr/bin/tmux new-session -d -s gnsyncbot
       /usr/bin/tmux send-keys -t gnsyncbot "cd /home/botusername" C-m

       # Get depot_tools on path (for goma_ctl, ninja, ...)
       /usr/bin/tmux send-keys -t gnsyncbot "source ./.bashrc" C-m

       /usr/bin/tmux send-keys -t gnsyncbot "cd src/llvm-project" C-m
       /usr/bin/tmux send-keys -t gnsyncbot "../hack/llvmgnsyncbot/syncbot.sh" C-m
       
   On macOS, the default shell is `zsh` and `tmux` isn't preinstalled but `screen` is.
   Use this script instead (with paths adjusted), run `chmod +x` on it, and
   _start it via launchd_ (see below for how):

       #!/bin/bash
         
       # Start via ~/Library/LaunchAgents/start-gnbot.plist, see below.
       
       /bin/sleep 4  # Wait a bit for the network to come up.
     
       /usr/bin/screen -dmS gnsyncbot
       /usr/bin/screen -S gnsyncbot -X stuff "cd /Users/botusername
       " 
     
       # Get depot_tools on path (for goma_ctl, ninja, ...)
       /usr/bin/screen -S gnsyncbot -X stuff "source ./.zshrc
       "
     
       /usr/bin/screen -S gnsyncbot -X stuff "cd src/llvm-project
       " 
       /usr/bin/screen -S gnsyncbot -X stuff "../hack/llvmgnsyncbot/syncbot.sh
       "

   Things started with `@reboot` from crontab end up in some scheduling class
   where they don't get access to performance cores on Apple Silicon, so start it
   via launchd:

       % cat ~/Library/LaunchAgents/start-gnbot.plist 
       <?xml version="1.0" encoding="UTF-8"?>
       <!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
       <plist version="1.0">
       <dict>
          <key>Label</key>
          <string>start-gnbot</string>
          <key>ProgramArguments</key>
          <array><string>/Users/thakis/syncbotcron.sh</string></array>
          <key>RunAtLoad</key>
          <true/>
       </dict>
       </plist>

[1]: https://docs.google.com/document/d/1rRL-rWDyL0Nwr6SdQTkh1tf5kYcjDoJwKhHs8WJHQSc/
[rsync]: http://repo.msys2.org/msys/x86_64/rsync-3.1.2-2-x86_64.pkg.tar.xz

Server setup
------------

Create a user `llvm`, and create `~llvm/buildlog`. The builders will rsync build
logs to `~llvm/buildlog/{linux,mac,win}`.

Check out this repository as llvm to `~llvm/hack`.

To run `annotate.py` automatically every time a file appears, run
`hack/llvmgnsyncbot/watch.sh buildlog html hack > watch.out 2> watch.err < /dev/null &`
followed by `disown -h`.

`annotate.py` then writes static html files to `~llvm/html`.

Create symlinks in `/var/www/html` to these two directories.

The webserver runs nginx in the default debian configuration.

`/etc/nginx/sites-enabled/default` contains custom bits to serve `~llvm/html`
as `/`, and currently a fallback to serve `~llvm/buildlog` at `/buildlog`:

    server {
        listen 80 default_server;
        listen [::]:80 default_server;

        root /var/www/html/html;

        index summary.html;

        server_name _;

        location /buildlog {
                root /var/www/html;
                autoindex on;
        }

        location / {
                # First attempt to serve request as file, then
                # as directory, then fall back to displaying a 404.
                try_files $uri $uri/ =404;
        }
    }

After changing this config, run `nginx -s reload` to make nginx reload its
config.

This setup requires that the user running nginx can access all files and
directories in `~llvm/buildlog` and `~llvm/html` -- directories need to be
`+rx` for others, files need to be `+r` for others. An easy way to check this
is `namei -m ~llvm/buildlog/linux/1.txt`.
