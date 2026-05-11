#include <iostream>
#include <string>
#include "../core/fpsan_research.h"

int main() {
    ResearchAgent r;
    r.decompose_goal("AGI roadmap");
    std::string path = r.persist_plan();
    std::cout << "Plan path: " << path << "\n";
    std::cout << "Remaining: " << r.remaining() << "\n";

    while (r.remaining() > 0) {
        std::string res = r.run_next_step(false);
        std::cout << "Run result: " << res << "\n";
        std::cout << "Remaining: " << r.remaining() << "\n";
    }
    return 0;
}
