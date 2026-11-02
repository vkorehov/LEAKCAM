# test127: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Obstacles

- 2026-11-02, my helpers imported tests/test126 run.py for its functions, and the import ran its whole check with 0
  rounds (a "FAIL step 1" in the logs whenever the setup firmware was active): run.py now runs only as a script.
- The step 1 runner was started as `cd X && nohup python3 ... &`, so $! was the shell, not the runner (memory
  background-runner-pid); the runner itself (its child) ran fine.

- 2026-11-02, my own slip: the first server-down try waited for "handover: ..." and matched the command's own
  "lp_handover: on" echo, so it read the state after 2.3 s and started the server again while the check may still have
  been running. Rerun with the hand-over's own line ("handover: USB power ..."): clean.
