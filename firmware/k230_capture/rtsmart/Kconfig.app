config APP_ENABLE_LEAKCAM
    bool "LEAKCAM programs (BL616 link agent, wake algorithm, history, stream)"
    default n
    help
      Builds leakcam_agent, leakcam_wake, leakcam_hist and leakcam_stream
      (video with audio) into /sdcard/app. Needs
      MPP_ENABLE_SENSOR_OV5647 with CSI devices 0 and 2.
