#pragma once
// ============================================================
// Pillar / Phase 15 — Spatial engine surface declaration
//
// Runtime default: fpsan_module_registry.h → `fpsan::NullSpatial`
// (ISpatialEngine; zero-cost deterministic stub).
//
// Dedicated headers keep orchestrator codegen / gates decoupled from the
// monolithic Cortex TU while preserving a single authoritative interface shape
// (`fpsan_iface.h`).
// ============================================================

#include "fpsan_iface.h"

namespace fpsan {

using SpatialSurface = ISpatialEngine;

} // namespace fpsan
