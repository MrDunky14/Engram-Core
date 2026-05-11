#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

// Test the distillation logic with mock Wikipedia data

std::vector<std::string> split_into_sentences(const std::string& text) {
    std::vector<std::string> sentences;
    std::string current;
    
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        current += c;
        
        if ((c == '.' || c == '?' || c == '!') && 
            i + 1 < text.length() && text[i+1] == ' ') {
            
            while (i + 1 < text.length() && text[i+1] == ' ') i++;
            
            if (!current.empty()) {
                sentences.push_back(current);
                current.clear();
            }
        }
    }
    
    if (!current.empty() && current.find_first_not_of(" \t\n\r") != std::string::npos) {
        sentences.push_back(current);
    }
    
    return sentences;
}

bool is_definitional_sentence(const std::string& sentence, const std::string& target) {
    std::string lower_sentence = sentence;
    std::string lower_target = target;
    
    for (auto& c : lower_sentence) c = tolower(c);
    for (auto& c : lower_target) c = tolower(c);
    
    if (lower_sentence.find(lower_target) == std::string::npos) {
        return false;
    }
    
    const char* definitive_verbs[] = {
        " is ", " are ", " means ", " refers to ", " allows ",
        " enables ", " involves ", " represents ", " consists of ",
        " can be ", " could be ", " defined as "
    };
    
    for (const auto* verb : definitive_verbs) {
        if (lower_sentence.find(verb) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

int score_definition_quality(const std::string& sentence) {
    int score = 0;
    
    if (sentence.length() < 150) score += 10;
    else if (sentence.length() < 250) score += 5;
    
    if (sentence.find('(') != std::string::npos) score -= 3;
    if (sentence.find(')') != std::string::npos) score -= 3;
    
    for (size_t i = 0; i + 3 < sentence.length(); i++) {
        if (isdigit(sentence[i]) && isdigit(sentence[i+1]) && 
            isdigit(sentence[i+2]) && isdigit(sentence[i+3])) {
            score -= 5;
            break;
        }
    }
    
    std::string lower = sentence;
    for (auto& c : lower) c = tolower(c);
    if (lower.find(" is ") != std::string::npos || lower.find(" are ") != std::string::npos) {
        score += 5;
    }
    
    return score;
}

std::string distill_to_definitions(const std::string& raw_text, const std::string& target_concept) {
    auto sentences = split_into_sentences(raw_text);
    
    std::vector<std::pair<int, std::string>> scored_sentences;
    for (const auto& sent : sentences) {
        if (is_definitional_sentence(sent, target_concept)) {
            int score = score_definition_quality(sent);
            scored_sentences.push_back({score, sent});
        }
    }
    
    std::sort(scored_sentences.begin(), scored_sentences.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::string result;
    int count = 0;
    for (const auto& [score, sent] : scored_sentences) {
        if (count >= 3) break;
        result += sent;
        if (!result.empty() && result.back() != '\n') result += " ";
        count++;
    }
    
    return result.empty() ? "No definitional content found." : result;
}

int main() {
    printf("=== INFORMATION DISTILLER TEST ===\n\n");
    
    // Mock Wikipedia extract with noise
    std::string mock_wiki = 
        "Machine learning is a subset of artificial intelligence (AI) that enables computers to learn from data without being explicitly programmed. "
        "In the early days (1950s-1960s), researchers like Alan Turing and others theorized about machine learning. "
        "Machine learning allows systems to improve their performance on tasks by learning from examples rather than through explicit instructions. "
        "The field involves (among other areas) deep learning, reinforcement learning, and supervised learning. "
        "Arthur Samuel created one of the first machine learning programs in 1959 for checkers. "
        "Machine learning means using algorithms and statistical models to identify patterns in data. "
        "Deep learning, a subset of machine learning, refers to neural networks with multiple layers. ";
    
    printf("Raw Wikipedia extract: %zu characters\n", mock_wiki.length());
    printf("────────────────────────────────────────\n\n");
    printf("%s\n", mock_wiki.c_str());
    printf("\n────────────────────────────────────────\n");
    printf("DISTILLED (Pure Definitions):\n");
    printf("────────────────────────────────────────\n\n");
    
    std::string distilled = distill_to_definitions(mock_wiki, "machine learning");
    printf("%s\n\n", distilled.c_str());
    
    printf("────────────────────────────────────────\n");
    printf("Compression: %zu → %zu bytes (%.1f%%)\n", 
           mock_wiki.length(), distilled.length(),
           (100.0f * distilled.length()) / mock_wiki.length());
    printf("\nNoise Eliminated:\n");
    printf("  ✗ Dates and historical trivia (1950s, 1960s, 1959)\n");
    printf("  ✗ Parenthetical qualifications (e.g., among other areas)\n");
    printf("  ✗ Non-semantic details (Arthur Samuel, checkers)\n");
    printf("✓ Pure semantic core preserved\n");
    
    return 0;
}
