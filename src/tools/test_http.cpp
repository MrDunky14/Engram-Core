#include <cstdio>
#include <string>
#include "fpsan_winsock_http.h"

int main() {
    printf("Testing WinSock HTTP Client...\n");
    printf("Attempting to fetch Wikipedia article on 'AGI'...\n");
    
    std::string result = HTTPClient::fetch_wikipedia_summary("Artificial general intelligence");
    
    if (result.empty()) {
        printf("FAILED: No content received.\n");
        printf("Possible causes:\n");
        printf("- Network unavailable or blocked\n");
        printf("- DNS resolution failed\n");
        printf("- Wikipedia is down or unreachable\n");
    } else {
        printf("SUCCESS: Fetched %zu characters\n", result.length());
        printf("Preview (first 200 chars):\n");
        printf("%.200s\n", result.c_str());
    }
    
    return 0;
}
