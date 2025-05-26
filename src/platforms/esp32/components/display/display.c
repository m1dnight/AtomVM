#include "display.h"

#include "context.h"
#include "globalcontext.h"
#include "nifs.h"
#include <defaultatoms.h>
#include <erl_nif_priv.h>
#include <nifs.h>
#include <sdkconfig.h>
#include <esp_log.h>
#include <trace.h>
#include <esp32_sys.h>

#define NO_ALLOC_FLAGS 0

void display_nif_destroy(GlobalContext *global)
{
    return;
};

void display_nif_init(GlobalContext *global)
{
    return;
};

static term test_nif(Context *ctx, int argc, term argv[])
{
    return OK_ATOM;
};

static const struct Nif display_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = test_nif
};

const struct Nif *display_nif_get_nif(const char *nifname)
{
    TRACE("Locating nif for display %s ...\n", nifname);
    ESP_LOGE("NIF", "hello");
    return &display_nif;
};


REGISTER_NIF_COLLECTION(display_nif, display_nif_init, NULL, display_nif_get_nif);
