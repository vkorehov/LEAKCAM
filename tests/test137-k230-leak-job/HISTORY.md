# test137-k230-leak-job: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-12 (run.py, 109 s): **FAIL**

7/12 checks pass. Failed: same scene: same,same (changed,same in 2813 ms); an object on cam0: changed,same (changed,changed in 2814 ms); cam1 same again: same,same (the history was kept on the change) (changed,same in 2817 ms); 8 checks: all same,same (); 8 checks: the K230 program's heap does not grow (heap + [942080, 942080, 942080] B).


### Automated run 2026-11-12 (run.py, 118 s): **FAIL**

10/12 checks pass. Failed: 8 checks: all same,same (); 8 checks: the K230 program's heap does not grow (heap + [942080, 942080] B).


### Automated run 2026-11-12 (run.py, 104 s): **FAIL**

9/11 checks pass. Failed: 8 checks: all same,same (); 8 checks: the K230 program's heap does not grow (heap + [942080, 942080, 942080, 942080] B).


### Automated run 2026-11-12 (run.py, 83 s): **FAIL**

10/11 checks pass. Failed: 8 checks: the K230 program's heap does not grow (heap + [942080, 942080, 942080, 942080, 954368, 917504] B).


### Automated run 2026-11-12 (run.py, 19 s): pass

11/11 checks pass.


### Automated run 2026-11-11 (run.py, 21 s): pass

11/11 checks pass.


### Automated run 2026-11-10 (run.py, 21 s): pass

11/11 checks pass.


### Automated run 2026-11-09 (run.py, 19 s): pass

11/11 checks pass.


### Automated run 2026-11-09 (run.py, 19 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 22 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 22 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 18 s): **FAIL**

10/11 checks pass. Failed: no history: DONE new,new (same,new in 112 ms).


### Automated run 2026-11-07 (run.py, 18 s): pass

11/11 checks pass.


### Automated run 2026-11-07 (run.py, 11 s): **FAIL**

10/11 checks pass. Failed: 8 checks: the K230 program's heap does not grow (heap + none B).


## Obstacles

- **2026-11-07, the job's console lines lost:** `r.k("")` (board.cmd) drains the port for 0.3 s before it writes, so
  the job's lines that came meanwhile were thrown away and the heap check found none (FAIL without a fault). Read
  passively instead: `cmd(fd, None, secs)` sends nothing and keeps what arrives.
- **2026-11-07, the scene file:** the K230 shell's `echo text file` appends and cannot create a file ("open file
  failed"); the job creates /sdcard/leakcam/sim empty and takes the last `cam<N>=` token.
