#include "../../clap/include/clap/entry.h"

bool lib_init(const char *path);
void lib_deinit();
const void* lib_get_factory(const char *id);

extern "C" const clap_plugin_entry_t clap_entry {
    .clap_version = CLAP_VERSION_INIT,
    .init = lib_init,
    
    .deinit = lib_deinit,
    
    .get_factory = lib_get_factory,
};
