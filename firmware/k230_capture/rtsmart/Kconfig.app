config APP_ENABLE_LEAKCAM
    bool "LEAKCAM capture (both OV5647, LED chains, NAND image history)"
    default n
    help
      Builds leakcam_capture and leakcam_hist into /sdcard/app. Needs
      MPP_ENABLE_SENSOR_OV5647 with CSI devices 0 and 2.
