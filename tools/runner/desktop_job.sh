#!/bin/sh
# Runs on the test runner, in the logged-in desktop session, started by the scheduled task NightfireRunner (see
# remote.sh): runs the job file remote.sh wrote and leaves its output and exit code beside it.
JOBS=/c/nightfire-runner
cd "$JOBS" || exit 1
sh job.sh > job.log 2>&1
echo $? > done
