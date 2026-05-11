#pragma once
// ============================================================
// FP-SAN Phase 17: VOICE — Engram Core speaks out loud.
// fpsan_voice.h — Windows SAPI Text-to-Speech integration.
//
// Uses the Windows Speech API (SAPI) to synthesize speech.
// Zero external dependencies — built into every Windows install.
//
// Engram Core no longer just prints text. It SPEAKS.
// ============================================================

#include <sapi.h>
#include <cstdio>
#include <cstring>

// Link against SAPI
#pragma comment(lib, "ole32.lib")

struct VoiceSystem {
    ISpVoice* pVoice;
    bool      initialized;
    bool      enabled;
    int       rate;     // Speech rate: -10 (slow) to 10 (fast), 0 = default

    void init() {
        pVoice = nullptr;
        initialized = false;
        enabled = true;
        rate = 2; // Slightly faster than default — feels more AI-like

        // Initialize COM
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            fprintf(stderr, "[Voice] COM initialization failed.\n");
            return;
        }

        // Create SAPI voice instance
        hr = CoCreateInstance(
            CLSID_SpVoice, NULL, CLSCTX_ALL,
            IID_ISpVoice, (void**)&pVoice
        );

        if (SUCCEEDED(hr) && pVoice) {
            pVoice->SetRate(rate);
            initialized = true;
            printf("  [Voice] SAPI Text-to-Speech active.\n");
        } else {
            fprintf(stderr, "[Voice] SAPI initialization failed.\n");
        }
    }

    // Speak text asynchronously (doesn't block the cognitive loop)
    void speak(const char* text) {
        if (!initialized || !enabled || !pVoice) return;
        if (!text || text[0] == '\0') return;

        // Convert UTF-8 to wide string for SAPI
        wchar_t wtext[1024];
        int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 1024);
        if (len <= 0) return;

        // SPF_ASYNC = non-blocking, SPF_PURGEBEFORESPEAK = interrupt previous
        pVoice->Speak(wtext, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    }

    // Speak and wait for completion (blocking)
    void speak_sync(const char* text) {
        if (!initialized || !enabled || !pVoice) return;
        if (!text || text[0] == '\0') return;

        wchar_t wtext[1024];
        int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 1024);
        if (len <= 0) return;

        pVoice->Speak(wtext, SPF_PURGEBEFORESPEAK, NULL);
    }

    // Returns true while SAPI is still synthesising speech.
    bool is_speaking() const {
        if (!initialized || !pVoice) return false;
        SPVOICESTATUS st{};
        if (FAILED(pVoice->GetStatus(&st, nullptr))) return false;
        return (st.dwRunningState & SPRS_IS_SPEAKING) != 0;
    }

    // Stop any current speech
    void stop() {
        if (!initialized || !pVoice) return;
        // Speak empty string with purge to stop
        pVoice->Speak(L"", SPF_PURGEBEFORESPEAK, NULL);
    }

    void set_rate(int new_rate) {
        rate = new_rate;
        if (rate < -10) rate = -10;
        if (rate > 10) rate = 10;
        if (pVoice) pVoice->SetRate(rate);
    }

    void toggle() {
        enabled = !enabled;
        printf("  [Voice] %s\n", enabled ? "ENABLED" : "MUTED");
    }

    void destroy() {
        if (pVoice) {
            pVoice->Release();
            pVoice = nullptr;
        }
        CoUninitialize();
        initialized = false;
    }
};
