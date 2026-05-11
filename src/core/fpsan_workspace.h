#pragma once
// ============================================================
// FP-SAN Phase 5: DESKTOP ROUTING
// fpsan_workspace.h — Win32 main-desktop binding.
//
// Routes JARVIS into the user's active desktop so windowed actions
// are visible in the main workspace instead of a hidden desktop.
// ============================================================

#include <windows.h>
#include <cstdio>

struct WorkspaceCortex {
    HDESK hDesktop;
    HDESK hOriginalDesktop;
    bool active;

    void init() {
        active = false;
        hDesktop = NULL;
        hOriginalDesktop = GetThreadDesktop(GetCurrentThreadId());

        // Stay on the current interactive desktop so motor actions are visible.
        active = false;
        printf("[WorkspaceCortex] Using main interactive desktop (no hidden desktop).\n");
    }

    void shutdown() {
        if (active && hDesktop) {
            CloseDesktop(hDesktop);
            hDesktop = NULL;
            active = false;
            printf("[WorkspaceCortex] Desktop binding released.\n");
        }
    }

    // No-op in main-desktop mode; retained for compatibility with existing call sites.
    bool bind_thread() {
        return true;
    }
};

extern WorkspaceCortex g_workspace;
