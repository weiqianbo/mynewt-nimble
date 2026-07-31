/*
 * Stubs for mesh settings functions when MYNEWT_VAL_BLE_MESH_SETTINGS is disabled
 * but CDB is enabled.
 */

#include "syscfg/syscfg.h"

#if !MYNEWT_VAL(BLE_MESH_SETTINGS)

#include <stdint.h>
#include <stddef.h>

/* Include settings.h from mesh/src directory */
#include "settings.h"

int settings_name_next(char *name, char **next)
{
    int rc = 0;

    if (next) {
        *next = NULL;
    }

    if (!name) {
        return 0;
    }

    while ((*name != '\0') && (*name != '=') &&
           (*name != '/')) {
        rc++;
        name++;
    }

    if (*name == '/') {
        if (next) {
            *next = name + 1;
        }
        return rc;
    }

    return rc;
}

void bt_mesh_settings_store_schedule(enum bt_mesh_settings_flag flag)
{
    /* No-op: settings not enabled */
}

void bt_mesh_settings_store_cancel(enum bt_mesh_settings_flag flag)
{
    /* No-op: settings not enabled */
}

void bt_mesh_settings_init(void)
{
    /* No-op: settings not enabled */
}

#endif /* !MYNEWT_VAL(BLE_MESH_SETTINGS) */