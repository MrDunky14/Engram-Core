#pragma once
// ============================================================
// Research program R2 — Perimeter translation (token string ⇄ interior node ID)
// Interior reasoning should prefer opaque IDs; English is I/O projection only.
// ============================================================

#include "fpsan_language.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace fpsan {

class TranslationCortex {
public:
    void clear() noexcept {
        word_to_id_.clear();
        id_to_word_.clear();
    }

    /// Register or refresh mapping for an active lexical cluster.
    void bind_label(int cluster_id, const char* word, const LanguageCortex* cortex) noexcept {
        if (cluster_id < 0 || !word || !word[0]) return;
        const char* lab = cortex ? cortex->get_word(cluster_id) : word;
        if (!lab || !lab[0]) lab = word;
        std::string k(lab);
        word_to_id_[k] = cluster_id;
        id_to_word_[cluster_id] = std::move(k);
    }

    [[nodiscard]] int id_for_token(const char* tok) const noexcept {
        if (!tok) return -1;
        auto it = word_to_id_.find(std::string(tok));
        if (it == word_to_id_.end()) return -1;
        return it->second;
    }

    [[nodiscard]] const char* label_for_id(int id) const noexcept {
        auto it = id_to_word_.find(id);
        if (it == id_to_word_.end()) return "";
        return it->second.c_str();
    }

private:
    std::unordered_map<std::string, int> word_to_id_;
    std::unordered_map<int, std::string> id_to_word_;
};

} // namespace fpsan
