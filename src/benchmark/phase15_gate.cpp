// Phase 15 gate — pillar interface registration sanity (five surfaces).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "fpsan_metamorphic.h"
#include "fpsan_module_registry.h"
#include "fpsan_recompiler_iface.h"
#include "fpsan_spatial_iface.h"

#include <cstdio>

int main() {
    fpsan::ModuleRegistry orch;
    fpsan::NullSensor            sensor;
    fpsan::NullActuator          actuator;
    fpsan::NullSpeakerIface      speaker;
    fpsan::NullSpatial           spatial;
    MetamorphicEngine            metamorphic_engine;
    fpsan::MetamorphicRecompilerAdapter recompiler(&metamorphic_engine);

    orch.register_module(&sensor);
    orch.register_module(&actuator);
    orch.register_module(&speaker);
    orch.register_module(&spatial);
    orch.register_module(&recompiler);

    if (orch.module_count() != 5u) {
        printf("FAIL expected 5 modules, got %u\n", orch.module_count());
        return 1;
    }
    orch.init_all();
    fpsan::ModuleTickContext ctx{42, 0, 999999};
    orch.tick_health_all(ctx, /*fail_limit*/ 32);
    orch.shutdown_all();

    (void)sizeof(fpsan::SpatialSurface*);
    puts("PASS");
    return 0;
}
