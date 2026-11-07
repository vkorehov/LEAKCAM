#include "switch_to_setup.h"

#include <stdio.h>
#include <string.h>

#include "easyflash.h"
#include "shell.h"

#include "FreeRTOS.h"
#include "task.h"

#include "k230_session.h"
#include "metrics.h"
#include "slot.h"

void switch_to_setup(const char *why)
{
    if (why && ef_set_env_blob("setup_why", why, strlen(why)) != EF_NO_ERR)
        printf("switch_to_setup: setup_why not stored, switching anyway\r\n");
    printf("switch_to_setup: %s\r\n", why ? why : "no network stored");
    if (k230_session_active()) {            /* the K230 off first: the session counted in the check-in (metrics.h) */
        k230_session_stop("to the setup firmware");
        while (k230_session_active())
            vTaskDelay(pdMS_TO_TICKS(50));
    }
    metrics_switch();
    if (slot_switch() != 0)                 /* returns only on a failed partition-table write */
        printf("switch_to_setup: slot switch failed, staying\r\n");
}

/* as the triggers do (tests/test117 without stopping the server) */
static void cmd_switch_to_setup(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "lost") || !strcmp(argv[1], "unused")))
        switch_to_setup(argv[1]);
    else
        printf("switch_to_setup: lost | unused\r\n");
}
SHELL_CMD_EXPORT_ALIAS(cmd_switch_to_setup, switch_to_setup, to the setup firmware as on a lost presence / no user left [lost | unused]);
