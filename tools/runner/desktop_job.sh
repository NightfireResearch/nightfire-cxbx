#!/bin/sh
# Runs on the test runner, in the logged-in desktop session, started by a scheduled task (see remote.sh): runs the job
# file remote.sh wrote and leaves its output and exit code beside it. With a slot number, that slot's job
# (NightfireRunner<slot>, the job files in slot<slot>/); without one, the serial job.
JOBS=/c/nightfire-runner${1:+/slot$1}
cd "$JOBS" || exit 1
sh job.sh > job.log 2>&1
echo $? > done
