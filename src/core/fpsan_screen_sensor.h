#pragma once
// ============================================================
// FP-SAN Phase 16B + Bug 2 fix: SCREEN SENSOR & PROPRIOCEPTIVE CORTEX
// fpsan_screen_sensor.h — JARVIS gets STRUCTURAL eyes.
//
// Three layers of perception, in order of priority:
//   1. PROPRIOCEPTION: OS-level state awareness via Win32 APIs
//      (GetForegroundWindow, GetWindowText, GetWindowRect).
//      The "internal body sense" — what window is active.
//
//   2. UIAUTOMATION READER  (Bug 2 — promoted to primary):
//      Walks the Windows accessibility tree of the foreground
//      window and extracts every Name/Value string + bounding box.
//      JARVIS now reads the structural DOM of the OS, not pixels.
//      This is what lets JARVIS read Notepad contents, Edge search
//      results, and IDE state without an ML-based OCR model.
//
//   3. FOVEATED MOTION SENSOR: 28x28 BitBlt + frame-difference.
//      Demoted to a coarse "something changed" signal used by the
//      efference-copy check.  No semantic content extracted from it.
//
// All native Win32 + COM. No Python, no third-party deps.
// ============================================================

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// ============================================================
// PROPRIOCEPTIVE CORTEX
// Internal OS state awareness — the "body sense" of JARVIS.
// This is computationally free and infinitely more reliable
// than trying to read pixels to determine window state.
// ============================================================
struct ProprioceptiveCortex {
    char  foreground_title[256];    // Current foreground window title
    HWND  foreground_hwnd;          // Current foreground window handle
    RECT  foreground_rect;          // Window position and size
    bool  foreground_visible;       // Is the foreground window visible?
    int   foreground_text_length;   // Text length (for edit controls)
    DWORD foreground_pid;           // Process ID of foreground app

    char  prev_title[256];          // Previous tick's title (for change detection)
    bool  title_changed;            // Did the foreground window change this tick?

    void init() {
        foreground_title[0] = '\0';
        prev_title[0] = '\0';
        foreground_hwnd = NULL;
        memset(&foreground_rect, 0, sizeof(RECT));
        foreground_visible = false;
        foreground_text_length = 0;
        foreground_pid = 0;
        title_changed = false;
    }

    // Update proprioceptive state — call every visual tick (~100ms)
    void tick() {
        // Save previous state for change detection
        strncpy(prev_title, foreground_title, 255);
        prev_title[255] = '\0';

        // Get foreground window
        foreground_hwnd = GetForegroundWindow();
        if (!foreground_hwnd) {
            foreground_title[0] = '\0';
            foreground_visible = false;
            title_changed = (prev_title[0] != '\0');
            return;
        }

        // Get window title
        GetWindowTextA(foreground_hwnd, foreground_title, 256);

        // Get window rect (position + size)
        GetWindowRect(foreground_hwnd, &foreground_rect);

        // Is the window visible?
        foreground_visible = IsWindowVisible(foreground_hwnd) ? true : false;

        // Get process ID
        GetWindowThreadProcessId(foreground_hwnd, &foreground_pid);

        // Detect title change
        title_changed = (strcmp(foreground_title, prev_title) != 0);
    }

    // Check if a specific window is in the foreground (case-insensitive partial match)
    bool is_foreground(const char* partial_title) {
        if (!foreground_hwnd || foreground_title[0] == '\0') return false;

        // Case-insensitive search
        char lower_title[256];
        strncpy(lower_title, foreground_title, 255);
        lower_title[255] = '\0';
        for (int i = 0; lower_title[i]; i++) lower_title[i] = (char)tolower(lower_title[i]);

        char lower_search[256];
        strncpy(lower_search, partial_title, 255);
        lower_search[255] = '\0';
        for (int i = 0; lower_search[i]; i++) lower_search[i] = (char)tolower(lower_search[i]);

        return strstr(lower_title, lower_search) != nullptr;
    }

    // Get window dimensions
    int width()  { return foreground_rect.right - foreground_rect.left; }
    int height() { return foreground_rect.bottom - foreground_rect.top; }

    // Print current state to console
    void print_status() {
        printf("  [Proprioception]\n");
        printf("    Window:  \"%s\"\n", foreground_title[0] ? foreground_title : "(none)");
        printf("    Visible: %s\n", foreground_visible ? "YES" : "NO");
        printf("    Size:    %dx%d\n", width(), height());
        printf("    Pos:     (%ld, %ld)\n", foreground_rect.left, foreground_rect.top);
        printf("    PID:     %lu\n", foreground_pid);
        printf("    Changed: %s\n", title_changed ? "YES" : "no");
    }
};

// ============================================================
// UIAUTOMATION READER  (Bug 2: structural eyes)
// Reads the Windows accessibility tree of the foreground window.
// This is the PRIMARY perception path.  Pixels are secondary.
//
// What we extract per UIA element:
//   - Name (label or visible text)
//   - ControlType (button, edit, document, window, ...)
//   - Value (for edit/document patterns)
//   - BoundingRect (where it lives on screen)
//
// Cost discipline: the tree is bounded by depth and node count
// per call so a deep Edge tab never spikes worst_tick_ms.
// ============================================================

// One observed accessibility element.  Phase 7 will spawn a graph node
// per element and wire EDGE_VISUAL_CHILD between parent and child.
struct UIAElement {
    std::string name;        // UTF-8
    std::string value;       // UTF-8 (edit/document content if available)
    int         control_type{0};  // UIA_*ControlTypeId
    int         depth{0};         // distance from root
    long        x{0}, y{0}, w{0}, h{0};
};

struct UIAReader {
    IUIAutomation*           uia       = nullptr;
    IUIAutomationTreeWalker* walker    = nullptr;
    bool                     ok        = false;
    // We only call CoUninitialize() if WE were the ones who
    // initialised COM on this thread (to avoid clobbering callers).
    bool                     com_owned = false;

    // Bounded tree-walk to keep tick latency under the 1.5ms gate.
    static constexpr int MAX_DEPTH = 6;
    static constexpr int MAX_NODES = 256;

    bool init() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr))            com_owned = true;
        else if (hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
            printf("[UIA] CoInitializeEx failed (0x%08lx)\n", (long)hr);
            return false;
        }

        hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                              __uuidof(IUIAutomation), (void**)&uia);
        if (FAILED(hr) || uia == nullptr) {
            printf("[UIA] CoCreateInstance(CUIAutomation) failed (0x%08lx)\n", (long)hr);
            return false;
        }

        hr = uia->get_ContentViewWalker(&walker);
        if (FAILED(hr) || walker == nullptr) {
            printf("[UIA] get_ContentViewWalker failed (0x%08lx)\n", (long)hr);
            return false;
        }

        ok = true;
        printf("[UIA] Reader initialised (content-view walker, depth<=%d, nodes<=%d).\n",
               MAX_DEPTH, MAX_NODES);
        return true;
    }

    void shutdown() {
        if (walker) { walker->Release(); walker = nullptr; }
        if (uia)    { uia->Release();    uia    = nullptr; }
        ok = false;
        if (com_owned) {
            CoUninitialize();
            com_owned = false;
        }
    }

    // Convert BSTR -> UTF-8 std::string and free the BSTR.
    static std::string consume_bstr(BSTR bs) {
        if (!bs) return std::string();
        int wlen = (int)SysStringLen(bs);
        if (wlen <= 0) { SysFreeString(bs); return std::string(); }
        int u8len = WideCharToMultiByte(CP_UTF8, 0, bs, wlen, nullptr, 0, nullptr, nullptr);
        std::string out(u8len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, bs, wlen, out.data(), u8len, nullptr, nullptr);
        SysFreeString(bs);
        return out;
    }

    // Pull text out of one element.  Returns concatenated Name + Value.
    void read_one(IUIAutomationElement* el, UIAElement& dst) const {
        BSTR name = nullptr;
        if (SUCCEEDED(el->get_CurrentName(&name)))
            dst.name = consume_bstr(name);

        CONTROLTYPEID ct = 0;
        el->get_CurrentControlType(&ct);
        dst.control_type = (int)ct;

        // Value pattern (edit / document content).
        VARIANT v; VariantInit(&v);
        if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &v))) {
            if (v.vt == VT_BSTR && v.bstrVal) {
                int wlen = (int)SysStringLen(v.bstrVal);
                if (wlen > 0) {
                    int u8 = WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, wlen,
                                                 nullptr, 0, nullptr, nullptr);
                    dst.value.assign(u8, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, wlen,
                                        dst.value.data(), u8, nullptr, nullptr);
                }
            }
            VariantClear(&v);
        }

        RECT bb{};
        if (SUCCEEDED(el->get_CurrentBoundingRectangle(&bb))) {
            dst.x = bb.left;  dst.y = bb.top;
            dst.w = bb.right  - bb.left;
            dst.h = bb.bottom - bb.top;
        }
    }

    // Recursive bounded walk.  Pushes UIAElement records into out.
    void walk(IUIAutomationElement* node, int depth,
              std::vector<UIAElement>& out) const {
        if (!node || !walker) return;
        if (depth > MAX_DEPTH) return;
        if ((int)out.size() >= MAX_NODES) return;

        UIAElement rec;
        rec.depth = depth;
        read_one(node, rec);
        if (!rec.name.empty() || !rec.value.empty())
            out.push_back(std::move(rec));

        IUIAutomationElement* child = nullptr;
        walker->GetFirstChildElement(node, &child);
        while (child && (int)out.size() < MAX_NODES) {
            walk(child, depth + 1, out);
            IUIAutomationElement* next = nullptr;
            walker->GetNextSiblingElement(child, &next);
            child->Release();
            child = next;
        }
        if (child) child->Release();
    }

    // Public: read the full bounded tree of the foreground window.
    std::vector<UIAElement> read_tree(HWND hwnd) const {
        std::vector<UIAElement> out;
        if (!ok || !uia || !hwnd) return out;
        IUIAutomationElement* root = nullptr;
        if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) return out;
        walk(root, 0, out);
        root->Release();
        return out;
    }

    // Public: concatenated UTF-8 text of every visible element.
    // This is what lets JARVIS *read* Notepad, Edge, and the IDE.
    std::string read_visible_text(HWND hwnd) const {
        std::string acc;
        for (const auto& e : read_tree(hwnd)) {
            if (!e.name.empty())  { acc += e.name;  acc += '\n'; }
            if (!e.value.empty()) { acc += e.value; acc += '\n'; }
        }
        return acc;
    }
};

// ============================================================
// FOVEATED SCREEN SENSOR  (now SECONDARY: motion-only)
// Kept intact so phase4_gate.cpp and the efference check still
// compile and run.  No semantic content is extracted from this
// path anymore — that job moved to UIAReader above.
// ============================================================
const int FOVEA_SIZE = 28;
const int FOVEA_DIM  = FOVEA_SIZE * FOVEA_SIZE; // 784 pixels

struct ScreenSensor {
    int8_t current_frame[FOVEA_DIM];   // Current 28x28 grayscale
    int8_t previous_frame[FOVEA_DIM];  // Previous frame (for temporal diff)
    int8_t spike_events[FOVEA_DIM];    // Temporal difference spikes
    int    total_spikes;               // Number of change events this tick
    bool   initialized;               // Has at least one frame been captured?

    void init() {
        memset(current_frame, 0, FOVEA_DIM);
        memset(previous_frame, 0, FOVEA_DIM);
        memset(spike_events, 0, FOVEA_DIM);
        total_spikes = 0;
        initialized = false;
    }

    // Capture the foreground window and downscale to 28x28 grayscale
    // Returns true if capture succeeded
    bool capture_window(HWND hwnd) {
        if (!hwnd || !IsWindowVisible(hwnd)) return false;

        // Get window rect
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        if (w <= 0 || h <= 0) return false;

        // Get device contexts
        HDC hdcScreen = GetDC(NULL);        // Entire screen
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        // Create a bitmap for the capture
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
        HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

        // BitBlt: copy window region from screen to memory DC
        BitBlt(hdcMem, 0, 0, w, h, hdcScreen, rect.left, rect.top, SRCCOPY);

        // Read pixels into a buffer
        BITMAPINFOHEADER bmi = {};
        bmi.biSize = sizeof(BITMAPINFOHEADER);
        bmi.biWidth = w;
        bmi.biHeight = -h;  // Top-down
        bmi.biPlanes = 1;
        bmi.biBitCount = 32; // BGRA
        bmi.biCompression = BI_RGB;

        // Allocate pixel buffer
        int pixel_count = w * h;
        BYTE* pixels = new BYTE[pixel_count * 4];
        GetDIBits(hdcMem, hBitmap, 0, h, pixels, (BITMAPINFO*)&bmi, DIB_RGB_COLORS);

        // Save previous frame for temporal differencing
        memcpy(previous_frame, current_frame, FOVEA_DIM);

        // Downscale to 28x28 grayscale using area averaging
        for (int fy = 0; fy < FOVEA_SIZE; fy++) {
            for (int fx = 0; fx < FOVEA_SIZE; fx++) {
                // Map fovea pixel to source region
                int sx_start = (fx * w) / FOVEA_SIZE;
                int sx_end   = ((fx + 1) * w) / FOVEA_SIZE;
                int sy_start = (fy * h) / FOVEA_SIZE;
                int sy_end   = ((fy + 1) * h) / FOVEA_SIZE;

                float sum = 0.0f;
                int count = 0;
                for (int sy = sy_start; sy < sy_end && sy < h; sy++) {
                    for (int sx = sx_start; sx < sx_end && sx < w; sx++) {
                        int idx = (sy * w + sx) * 4;
                        // Convert BGRA to grayscale: 0.299R + 0.587G + 0.114B
                        float gray = 0.114f * pixels[idx]     // B
                                   + 0.587f * pixels[idx + 1] // G
                                   + 0.299f * pixels[idx + 2]; // R
                        sum += gray;
                        count++;
                    }
                }
                // Quantize to 8 levels (0-7) for sensitive temporal differencing.
                // Binary (0/1) was too coarse — Notepad is all-white in both frames.
                // 8 levels allow detecting subtle changes like text appearing.
                float avg = (count > 0) ? (sum / count) : 0.0f;
                current_frame[fy * FOVEA_SIZE + fx] = (int8_t)(avg / 32.0f); // 0-7 range
            }
        }

        // Cleanup GDI resources
        delete[] pixels;
        SelectObject(hdcMem, hOld);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);

        // Compute temporal difference (spike events)
        compute_temporal_diff();

        initialized = true;
        return true;
    }

    // Compute temporal difference between current and previous frames
    // Generates ON/OFF spike events like a DVS (Dynamic Vision Sensor)
    void compute_temporal_diff() {
        total_spikes = 0;
        for (int i = 0; i < FOVEA_DIM; i++) {
            int diff = current_frame[i] - previous_frame[i];
            if (diff > 0)      spike_events[i] =  1; // ON spike (got brighter)
            else if (diff < 0) spike_events[i] = -1; // OFF spike (got darker)
            else               spike_events[i] =  0; // No change

            if (spike_events[i] != 0) total_spikes++;
        }
    }

    // Print current frame as ASCII art (28x28 → compact display)
    void print_ascii(const char* label = "FOVEA") {
        printf("  [%s] 28x28 capture (%d spike events)\n", label, total_spikes);
        // Gradient characters for 8 intensity levels
        const char* shades[] = {"  ", "..", "::", ";;", "++", "**", "##", "@@"};
        for (int y = 0; y < FOVEA_SIZE; y += 2) {
            printf("    ");
            for (int x = 0; x < FOVEA_SIZE; x++) {
                int val = current_frame[y * FOVEA_SIZE + x];
                if (val < 0) val = 0;
                if (val > 7) val = 7;
                printf("%s", shades[val]);
            }
            printf("\n");
        }
    }

    // Print temporal diff as ASCII art (shows what changed)
    void print_diff_ascii() {
        printf("  [TEMPORAL DIFF] (%d spikes)\n", total_spikes);
        for (int y = 0; y < FOVEA_SIZE; y += 2) {
            printf("    ");
            for (int x = 0; x < FOVEA_SIZE; x++) {
                int val = spike_events[y * FOVEA_SIZE + x];
                if (val > 0)       printf("++");  // ON event
                else if (val < 0)  printf("--");  // OFF event
                else               printf("  ");  // No change
            }
            printf("\n");
        }
    }
};

// ============================================================
// COMBINED VISUAL CORTEX (Proprioception + UIA + Foveated Motion)
// ============================================================
struct VisualSystem {
    ProprioceptiveCortex proprioception;
    UIAReader            uia;       // Bug 2: structural eyes (primary)
    ScreenSensor         fovea;     // Motion-only secondary sensor
    uint64_t             visual_tick_count;
    bool                 enabled;
    bool                 uia_ready;
    // Most recent UIA snapshot of the foreground window.
    // Phase 7 will spawn graph nodes from this list; for now the live
    // core can read it directly via read_visible_text() / last_tree.
    std::vector<UIAElement> last_tree;

    void init() {
        proprioception.init();
        fovea.init();
        uia_ready = uia.init();   // best-effort; falls back to motion-only
        visual_tick_count = 0;
        enabled = true;
    }

    void shutdown() {
        uia.shutdown();
    }

    // Run one visual tick — call every ~100ms from main loop.
    // UIA dominates; fovea runs in parallel as a coarse motion gate.
    void tick() {
        if (!enabled) return;
        visual_tick_count++;

        proprioception.tick();
        if (!proprioception.foreground_hwnd) {
            last_tree.clear();
            return;
        }

        if (uia_ready) {
            // PRIMARY: structural read of the foreground window.
            last_tree = uia.read_tree(proprioception.foreground_hwnd);
        }

        // SECONDARY: motion sensor, used by efference-copy.
        fovea.capture_window(proprioception.foreground_hwnd);
    }

    // Public helper: concatenated text JARVIS believes is on screen.
    // Sourced from UIA, never from pixel OCR.
    std::string read_visible_text() const {
        std::string acc;
        for (const auto& e : last_tree) {
            if (!e.name.empty())  { acc += e.name;  acc += '\n'; }
            if (!e.value.empty()) { acc += e.value; acc += '\n'; }
        }
        return acc;
    }

    // Verify that a specific window is active and visible
    bool verify_target(const char* partial_title) {
        proprioception.tick(); // Fresh read
        return proprioception.is_foreground(partial_title) && proprioception.foreground_visible;
    }

    // Check if the screen changed after a motor action (Efference Copy check)
    // Returns true if visual change was detected (spikes > threshold)
    bool detected_change(int spike_threshold = 5) {
        return fovea.total_spikes > spike_threshold;
    }
};
