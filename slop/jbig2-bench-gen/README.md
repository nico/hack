This contains the scripts for generating https://nico.github.io/jbig2-bench/

At first clone, run once:

    npm install

To produce a report, run:

    uv run run_tests.py

Deploy a report like so:

    ./deploy.sh git@github.com:nico/jbig2-bench.git

For now, this requires having all the tools installed locally, see config.json.

It'd be nice to auto-install them all via GitHub actions and automatically
run reports there eventually.
