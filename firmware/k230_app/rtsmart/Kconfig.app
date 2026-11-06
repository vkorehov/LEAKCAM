config APP_ENABLE_LEAKCAM
    bool "LEAKCAM product program (leakcam: the BL616 link, the USB setup relay)"
    default n
    help
      Builds leakcam into /sdcard/app, started at boot by RTT_AUTO_EXEC_CMD.
