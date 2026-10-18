# 10. NAND persistence, full disk, speed

- **`echo` cannot create files** -> RT-Thread's msh `echo` opens without O_CREAT -> create with `cp` first.
- **No checksum tool on the K230** -> byte-exact check with `hexdump` of a 100 KB file, compared on the host;
  5 MB files only by size (a dump would take hours at 115200).
- **Dump looked corrupted after boot** -> the agent's `READY not answered` lines interleave -> dump on a quiet console.
- Result: full disk handled cleanly, read 9.4 MB/s, write 1.8 MB/s, byte-exact after a power cycle.
