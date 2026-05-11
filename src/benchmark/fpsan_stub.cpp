// Stub implementations of personality coloring functions.
// Used by benchmarks that include fpsan_lexer.h but do NOT link
// against fpsan_live_core.cpp. The real implementations live in
// fpsan_live_core.cpp for the full JARVIS runtime.

#include "cluster_graph.h"
#include "fpsan_language.h"
#include <vector>
#include <utility>

void apply_personality_coloring(ClusterGraph*, LanguageCortex*,
                                 std::vector<std::pair<int,float>>&) {
    // No personality coloring in benchmark context.
}

void revert_personality_coloring(ClusterGraph*,
                                  const std::vector<std::pair<int,float>>&) {
    // No-op.
}
