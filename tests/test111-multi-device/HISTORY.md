# test111: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-01 (run.py, 128 s): pass

12/12 checks pass.

- phone USB port was the power source: swapped to sink


### Automated run 2026-11-01 09:52 (run.py, 137 s): pass

12/12 checks pass (first run).

- phone USB port was the power source: swapped to sink


## Obstacles

- 2026-11-01: a console command typed right after the board started checking in came back "command not found" (the Wi-Fi
  stack's log lines interleaved with it); run.py retries the `presence` read once. The same log flood cut test106's
  product detection short (it read `help`): phone.product() now asks `prov` itself.
