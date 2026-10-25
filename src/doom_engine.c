#define DOOM_IMPLEMENTATION
#define DOOM_IMPLEMENT_MALLOC
#define DOOM_IMPLEMENT_EXIT
#include "../third_party/doom/PureDOOM.h"

void aurora_doom_save_config(void) { M_SaveDefaults(); }
