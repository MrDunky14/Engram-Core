#pragma once
// ============================================================
// FP-SAN Phase 18B: ASYNCHRONOUS MOTOR CORTEX
// fpsan_motor.h — Native C++ OS embodiment via Windows API.
//
// Now features a dedicated background std::thread and a lock-free 
// SPSC queue with Epoch Tagging. The main 1kHz cognitive loop 
// is never blocked by OS keystroke delays.
//
// Code formatting macros (START_BLOCK, END_BLOCK) enable JARVIS
// to physically type Python syntax with correct indentation.
// ============================================================

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <algorithm>
#include "fpsan_screen_sensor.h"
#include "fpsan_workspace.h"

extern VisualSystem g_vision;

// Global counter: incremented each time type_string() aborts due to focus loss.
// fpsan_live_core.cpp polls this to spike the frustration drive.
inline std::atomic<int> g_motor_focus_aborts{0};

// ============================================================
// MOTOR ACTION TYPES
// ============================================================
enum MotorActionType : uint8_t {
    MOTOR_NONE          = 0,
    MOTOR_KEY_PRESS     = 1,   // Single keystroke
    MOTOR_KEY_CHORD     = 2,   // Ctrl+X, Alt+Tab, etc.
    MOTOR_TYPE_STRING   = 3,   // Type a full string
    MOTOR_LAUNCH_APP    = 4,   // CreateProcess
    MOTOR_FOCUS_WINDOW  = 5,   // FindWindow + SetForegroundWindow
    MOTOR_MOUSE_CLICK   = 6,   // Mouse click at (x, y)
    MOTOR_SLEEP_MS      = 7,   // Delay between actions
    MOTOR_START_BLOCK   = 8,   // Code formatting: type ':', Enter, auto-indent
    MOTOR_END_BLOCK     = 9,   // Code formatting: unindent
    MOTOR_HTTP_GET      = 10,  // Phase 3: WinSock HTTP GET primitive (text[] = URL)
};

// ============================================================
// MOTOR ACTION (A single atomic motor command)
// ============================================================
struct MotorAction {
    MotorActionType type;
    WORD            vkey;          // For KEY_PRESS / KEY_CHORD
    WORD            modifier;     // For KEY_CHORD (VK_CONTROL, VK_MENU, VK_SHIFT)
    char            text[256];    // For TYPE_STRING, LAUNCH_APP, FOCUS_WINDOW
    int             x, y;        // For MOUSE_CLICK
    int             delay_ms;    // For SLEEP_MS
};

// ============================================================
// MOTOR SEQUENCE (A chain of actions to execute in order)
// ============================================================
const int MAX_SEQUENCE_STEPS = 32;

struct MotorSequence {
    char name[64];
    MotorAction steps[MAX_SEQUENCE_STEPS];
    int step_count;

    void init(const char* seq_name) {
        strncpy(name, seq_name, 63);
        name[63] = '\0';
        step_count = 0;
    }

    void add_key(WORD vkey) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_KEY_PRESS;
        a.vkey = vkey;
        a.modifier = 0;
        a.delay_ms = 0;
    }

    void add_chord(WORD mod, WORD vkey) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_KEY_CHORD;
        a.vkey = vkey;
        a.modifier = mod;
        a.delay_ms = 0;
    }

    void add_string(const char* str) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_TYPE_STRING;
        strncpy(a.text, str, 255);
        a.text[255] = '\0';
        a.delay_ms = 0;
    }

    void add_launch(const char* app) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_LAUNCH_APP;
        strncpy(a.text, app, 255);
        a.text[255] = '\0';
        a.delay_ms = 0;
    }

    void add_focus(const char* window_title) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_FOCUS_WINDOW;
        strncpy(a.text, window_title, 255);
        a.text[255] = '\0';
        a.delay_ms = 0;
    }

    void add_delay(int ms) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_SLEEP_MS;
        a.delay_ms = ms;
    }

    void add_mouse(int mx, int my) {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_MOUSE_CLICK;
        a.x = mx;
        a.y = my;
        a.delay_ms = 0;
    }

    void add_start_block() {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_START_BLOCK;
        a.delay_ms = 0;
    }

    void add_end_block() {
        if (step_count >= MAX_SEQUENCE_STEPS) return;
        MotorAction& a = steps[step_count++];
        a.type = MOTOR_END_BLOCK;
        a.delay_ms = 0;
    }
};

// ============================================================
// MOTOR COMMAND QUEUE (Lock-Free SPSC Ring Buffer)
// ============================================================
struct MotorCommand {
    MotorAction action;
    uint64_t epoch;
};

const int MOTOR_QUEUE_SIZE = 256;

struct MotorQueue {
    MotorCommand buffer[MOTOR_QUEUE_SIZE];
    std::atomic<int> head;
    std::atomic<int> tail;

    void init() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    bool push(const MotorCommand& cmd) {
        int current_tail = tail.load(std::memory_order_relaxed);
        int next_tail = (current_tail + 1) % MOTOR_QUEUE_SIZE;
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        buffer[current_tail] = cmd;
        tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(MotorCommand& out_cmd) {
        int current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        out_cmd = buffer[current_head];
        head.store((current_head + 1) % MOTOR_QUEUE_SIZE, std::memory_order_release);
        return true;
    }
};

// ============================================================
// MOTOR CORTEX (The physical output layer)
// ============================================================
const int MAX_PROCEDURES = 32;

struct MotorCortex {
    MotorSequence procedures[MAX_PROCEDURES];
    int procedure_count;
    bool killed;  // Hardware kill-switch engaged

    // Code Formatting State
    int current_indent_level;

    // Asynchronous Execution
    MotorQueue queue;
    std::atomic<uint64_t> current_motor_epoch;
    std::thread motor_thread;
    std::atomic<bool> thread_running;

    void init() {
        procedure_count = 0;
        killed = false;
        current_indent_level = 0;
        queue.init();
        current_motor_epoch.store(1);
        thread_running.store(true);
        motor_thread = std::thread(&MotorCortex::motor_loop, this);
        printf("[MotorCortex] Initialized. Async Motor Thread running.\n");
    }

    void shutdown() {
        thread_running.store(false);
        if (motor_thread.joinable()) {
            motor_thread.join();
        }
    }

    void abort_sequence() {
        current_motor_epoch.fetch_add(1);
        printf("  [MotorCortex] Motor sequence aborted (Epoch incremented).\n");
    }

    bool queue_action(const MotorAction& action) {
        MotorCommand cmd;
        cmd.action = action;
        cmd.epoch = current_motor_epoch.load(std::memory_order_relaxed);
        return queue.push(cmd);
    }

    // ─────────────────────────────────────
    // HARDWARE KILL-SWITCH
    // Check if ESC is held — immediately halt all motor output.
    // ─────────────────────────────────────
    bool check_kill_switch() {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            if (!killed) {
                killed = true;
                abort_sequence();
                printf("\n  [!!! KILL SWITCH !!!] ESC detected. All motor output HALTED.\n");
            }
            return true;
        }
        return killed;
    }

    void reset_kill_switch() {
        killed = false;
        printf("  [MotorCortex] Kill switch reset. Motor output re-enabled.\n");
    }

    // ─────────────────────────────────────
    // ATOMIC MOTOR PRIMITIVES
    // ─────────────────────────────────────

    bool send_key(WORD vkey) {
        if (check_kill_switch()) return false;
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vkey;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vkey;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        return true;
    }

    bool send_chord(WORD modifier, WORD vkey) {
        if (check_kill_switch()) return false;
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = modifier;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vkey;
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = vkey;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = modifier;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
        return true;
    }

    bool type_string(const char* str, uint64_t epoch) {
        if (check_kill_switch()) return false;

        // Capture the target window before we start typing.
        // If focus leaves this window mid-sequence we abort immediately.
        HWND target_hwnd = GetForegroundWindow();

        for (int i = 0; str[i] != '\0'; i++) {
            if (epoch != current_motor_epoch.load()) return false; // Flush check
            if (check_kill_switch()) return false;

            // Focus-loss guard: abort and signal frustration.
            if (GetForegroundWindow() != target_hwnd) {
                printf("[Motor] Focus lost — aborting type_string at char %d.\n", i);
                g_motor_focus_aborts.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            char c = str[i];
            if (c == '\n') {
                send_key(VK_RETURN);
                Sleep(40);
                continue;
            }

            INPUT down = {};
            down.type = INPUT_KEYBOARD;
            down.ki.wVk = 0;
            down.ki.wScan = (WORD)c;
            down.ki.dwFlags = KEYEVENTF_UNICODE;
            SendInput(1, &down, sizeof(INPUT));
            Sleep(10);

            INPUT up = {};
            up.type = INPUT_KEYBOARD;
            up.ki.wVk = 0;
            up.ki.wScan = (WORD)c;
            up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &up, sizeof(INPUT));
            Sleep(40); // 40ms inter-char
        }
        return true;
    }

    void type_syntax(char c) {
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = 0;
        down.ki.wScan = (WORD)c;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        SendInput(1, &down, sizeof(INPUT));
        Sleep(10);
        INPUT up = {};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = 0;
        up.ki.wScan = (WORD)c;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &up, sizeof(INPUT));
    }

    bool launch_app(const char* app_name) {
        if (check_kill_switch()) return false;
        
        STARTUPINFOA si = { sizeof(STARTUPINFOA) };
        if (g_workspace.active) {
            si.lpDesktop = (LPSTR)"JarvisDesktop";
        }
        PROCESS_INFORMATION pi = { 0 };

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s", app_name);

        BOOL success = CreateProcessA(
            NULL,           // Application name
            cmd,            // Command line
            NULL,           // Process attributes
            NULL,           // Thread attributes
            FALSE,          // Inherit handles
            0,              // Creation flags
            NULL,           // Environment
            NULL,           // Current directory
            &si,            // Startup info
            &pi             // Process info
        );

        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            printf("  [MotorCortex] Launched: %s\n", app_name);
            return true;
        } else {
            // Fallback to ShellExecute if CreateProcess fails
            HINSTANCE result = ShellExecuteA(NULL, "open", app_name, NULL, NULL, SW_SHOWNORMAL);
            if ((intptr_t)result > 32) {
                printf("  [MotorCortex] Launched via ShellExecute: %s\n", app_name);
                return true;
            }
            printf("  [MotorCortex] FAILED to launch: %s (error %lu)\n", app_name, GetLastError());
            return false;
        }
    }

    static BOOL CALLBACK enum_windows_callback(HWND hwnd, LPARAM lParam) {
        char** args = (char**)lParam;
        char* search = args[0];
        HWND* found = (HWND*)args[1];

        char title[256];
        GetWindowTextA(hwnd, title, 256);
        if (title[0] == '\0') return TRUE;

        for (int i = 0; title[i]; i++) title[i] = (char)tolower(title[i]);
        char lower_search[256];
        strncpy(lower_search, search, 255);
        lower_search[255] = '\0';
        for (int i = 0; lower_search[i]; i++) lower_search[i] = (char)tolower(lower_search[i]);

        if (strstr(title, lower_search)) {
            *found = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    bool focus_window(const char* partial_title) {
        if (check_kill_switch()) return false;
        HWND found = NULL;
        char search_buf[256];
        strncpy(search_buf, partial_title, 255);
        search_buf[255] = '\0';
        char* args[2] = { search_buf, (char*)&found };

        // Deterministic Polling: 200 iterations * 10ms = 2000ms timeout
        for (int i = 0; i < 200; i++) {
            if (check_kill_switch()) return false;
            
            found = NULL;
            EnumWindows(enum_windows_callback, (LPARAM)args);
            
            if (found) {
                SetForegroundWindow(found);
                Sleep(50); // Small biological buffer just to let it gain focus before typing
                printf("  [MotorCortex] Focused window: %s (found in %d ms)\n", partial_title, i * 10);
                return true;
            }
            Sleep(10);
        }

        printf("  [MotorCortex] Window not found: %s (Timed out after 2000ms)\n", partial_title);
        return false;
    }

    static BOOL CALLBACK list_windows_callback(HWND hwnd, LPARAM lParam) {
        if (!IsWindowVisible(hwnd)) return TRUE;
        char title[256];
        GetWindowTextA(hwnd, title, 256);
        if (title[0] != '\0' && strlen(title) > 1) {
            int* count = (int*)lParam;
            printf("    [%d] %s\n", *count, title);
            (*count)++;
        }
        return TRUE;
    }

    int list_windows() {
        int count = 0;
        EnumWindows(list_windows_callback, (LPARAM)&count);
        return count;
    }

    bool mouse_click(int x, int y) {
        if (check_kill_switch()) return false;
        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        int nx = (x * 65535) / screen_w;
        int ny = (y * 65535) / screen_h;

        INPUT inputs[3] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dx = nx;
        inputs[0].mi.dy = ny;
        inputs[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[2].type = INPUT_MOUSE;
        inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(3, inputs, sizeof(INPUT));
        return true;
    }

    // ─────────────────────────────────────
    // ASYNCHRONOUS BACKGROUND THREAD
    // ─────────────────────────────────────
    void motor_loop() {
        g_workspace.bind_thread(); // Main-desktop mode: compatibility no-op

        while (thread_running.load()) {
            MotorCommand cmd;
            if (queue.pop(cmd)) {
                // Epoch check (Lock-Free Flush)
                if (cmd.epoch != current_motor_epoch.load()) {
                    continue; // Discard stale command
                }
                
                if (check_kill_switch()) {
                    abort_sequence();
                    continue;
                }

                bool ok = true;
                switch (cmd.action.type) {
                    case MOTOR_KEY_PRESS:   ok = send_key(cmd.action.vkey); break;
                    case MOTOR_KEY_CHORD:   ok = send_chord(cmd.action.modifier, cmd.action.vkey); break;
                    case MOTOR_TYPE_STRING: ok = type_string(cmd.action.text, cmd.epoch); break;
                    case MOTOR_LAUNCH_APP:  ok = launch_app(cmd.action.text); break;
                    case MOTOR_FOCUS_WINDOW:ok = focus_window(cmd.action.text); break;
                    case MOTOR_MOUSE_CLICK: ok = mouse_click(cmd.action.x, cmd.action.y); break;
                    case MOTOR_SLEEP_MS:    Sleep(cmd.action.delay_ms); break;
                    case MOTOR_START_BLOCK: 
                        type_syntax(':');
                        send_key(VK_RETURN);
                        Sleep(40);
                        current_indent_level++;
                        for(int i = 0; i < current_indent_level; i++) {
                            send_key(VK_TAB);
                            Sleep(20);
                        }
                        break;
                    case MOTOR_END_BLOCK:
                        current_indent_level = std::max(0, current_indent_level - 1);
                        send_key(VK_RETURN);
                        Sleep(40);
                        send_key(VK_BACK);
                        Sleep(20);
                        break;
                    default: break;
                }
                if (cmd.action.type != MOTOR_SLEEP_MS) Sleep(50);
            } else {
                Sleep(1); // Yield when empty
            }
        }
    }

    // ─────────────────────────────────────
    // PROCEDURE EXECUTION ENGINE
    // ─────────────────────────────────────

    int register_procedure(const MotorSequence& seq) {
        if (procedure_count >= MAX_PROCEDURES) return -1;
        procedures[procedure_count] = seq;
        return procedure_count++;
    }

    MotorSequence* find_procedure(const char* name) {
        for (int i = 0; i < procedure_count; i++) {
            if (strcmp(procedures[i].name, name) == 0) return &procedures[i];
        }
        return nullptr;
    }

    // ─────────────────────────────────────
    // EXECUTE_PROCEDURE_WITH_FALLBACK
    // Queues primary procedure steps, waits for queue to drain,
    // checks for visual failure, and falls back to fallback procedure if needed.
    // ─────────────────────────────────────
    bool execute_procedure_with_fallback(const char* primary_name, const char* fallback_name) {
        MotorSequence* primary = find_procedure(primary_name);
        if (!primary) {
            printf("  [MotorCortex] Fallback Error: Primary procedure '%s' not found.\n", primary_name);
            return false;
        }

        printf("  [MotorCortex] [Fallback] Attempting Primary: %s\n", primary_name);
        
        // Queue primary sequence steps
        uint64_t epoch = current_motor_epoch.load(std::memory_order_acquire);
        for (int i = 0; i < primary->step_count; i++) {
            MotorCommand cmd;
            cmd.action = primary->steps[i];
            cmd.epoch = epoch;
            while (!queue.push(cmd) && thread_running.load()) { 
                Sleep(1); 
            }
        }
        
        // Wait for queue to drain (all primary steps executed)
        int spin_count = 0;
        while (!is_idle() && thread_running.load() && spin_count < 2000) { // Max 2s wait
            Sleep(10);
            spin_count++;
        }
        
        printf("  [MotorCortex] [Fallback] Queue drained after %d ms. Verifying foreground target...\n", spin_count * 10);

        char expected_title[64] = {0};
        const char* verify_title = nullptr;
        if (strstr(primary_name, "notepad") != nullptr) {
            strncpy(expected_title, "notepad", sizeof(expected_title) - 1);
            verify_title = expected_title;
        } else if (strstr(primary_name, "clock") != nullptr) {
            strncpy(expected_title, "clock", sizeof(expected_title) - 1);
            verify_title = expected_title;
        } else if (strstr(primary_name, "calc") != nullptr) {
            strncpy(expected_title, "calculator", sizeof(expected_title) - 1);
            verify_title = expected_title;
        }

        bool target_verified = false;
        if (verify_title) {
            for (int i = 0; i < 150; i++) {
                g_vision.proprioception.tick();
                if (g_vision.verify_target(verify_title)) {
                    target_verified = true;
                    break;
                }
                Sleep(10);
            }
        }

        if (!target_verified && verify_title) {
            printf("  [MotorCortex] [Fallback] Primary '%s' was not verified in foreground. Initiating Fallback: %s\n",
                   primary_name, fallback_name);
            
            MotorSequence* fallback = find_procedure(fallback_name);
            if (!fallback) {
                printf("  [MotorCortex] [Fallback] ERROR: Fallback procedure '%s' NOT FOUND!\n", fallback_name);
                printf("  [MotorCortex] [Fallback] Available procedures: ");
                for (int i = 0; i < procedure_count; i++) {
                    printf("%s ", procedures[i].name);
                }
                printf("\n");
                return false;
            }
            
            printf("  [MotorCortex] [Fallback] Found fallback procedure '%s' with %d steps. Queuing...\n", 
                   fallback_name, fallback->step_count);
            
            epoch = current_motor_epoch.load(std::memory_order_acquire);
            for (int i = 0; i < fallback->step_count; i++) {
                MotorCommand cmd;
                cmd.action = fallback->steps[i];
                cmd.epoch = epoch;
                while (!queue.push(cmd) && thread_running.load()) { 
                    Sleep(1); 
                }
            }
            printf("  [MotorCortex] [Fallback] Fallback procedure queued successfully: %s (%d steps)\n", 
                   fallback_name, fallback->step_count);
            return true;
        }
        
        printf("  [MotorCortex] [Fallback] Primary '%s' verified in foreground (no fallback needed).\n", primary_name);
        return true;
    }

    bool is_idle() {
        return queue.head.load(std::memory_order_acquire) == queue.tail.load(std::memory_order_acquire);
    }

    bool queue_string(const char* str) {
        int len = strlen(str);
        uint64_t epoch = current_motor_epoch.load();
        
        for (int i = 0; i < len; i += 250) {
            MotorCommand cmd;
            cmd.action.type = MOTOR_TYPE_STRING;
            cmd.epoch = epoch;
            cmd.action.delay_ms = 0;
            strncpy(cmd.action.text, str + i, 250);
            cmd.action.text[250] = '\0';
            while (!queue.push(cmd) && thread_running.load()) {
                Sleep(1);
            }
        }
        return true;
    }

    bool queue_key(WORD vkey) {
        uint64_t epoch = current_motor_epoch.load();
        MotorCommand cmd;
        cmd.action.type = MOTOR_KEY_PRESS;
        cmd.action.vkey = vkey;
        cmd.action.modifier = 0;
        cmd.action.delay_ms = 0;
        cmd.epoch = epoch;
        while (!queue.push(cmd) && thread_running.load()) {
            Sleep(1);
        }
        return true;
    }

    bool execute_procedure(const char* name) {
        MotorSequence* seq = find_procedure(name);
        if (!seq) {
            printf("  [MotorCortex] Unknown procedure: %s\n", name);
            return false;
        }

        printf("  [MotorCortex] Queuing procedure: %s (%d steps)\n", name, seq->step_count);
        uint64_t epoch = current_motor_epoch.load();
        
        for (int i = 0; i < seq->step_count; i++) {
            MotorCommand cmd;
            cmd.action = seq->steps[i];
            cmd.epoch = epoch;
            while (!queue.push(cmd) && thread_running.load()) {
                Sleep(1); // Backpressure if queue is full
            }
        }
        return true;
    }

    // ─────────────────────────────────────
    // BUILT-IN PROCEDURES
    // ─────────────────────────────────────
    void bootstrap_procedures() {
        {
            MotorSequence s;
            s.init("open_notepad_api");
            s.add_launch("notepad.exe");
            // Direct OS API approach (fast but brittle on Win11 UWP apps)
            s.add_focus("notepad");
            s.add_delay(50); // tiny stabilization delay after focus
            register_procedure(s);
        }
        {
            // FALLBACK: Physical UI Interaction (slower but robust against Win11 UWP issues)
            MotorSequence s;
            s.init("open_notepad_ui");
            s.add_key(VK_LWIN);            // Open Start Menu
            s.add_delay(600);              // Wait for animation
            s.add_string("notepad");       // Type to search
            s.add_delay(800);              // Wait for search indexing
            s.add_key(VK_RETURN);          // Hit Enter
            s.add_delay(500);              // Wait for launch
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("open_notepad");  // Legacy alias for backward compatibility
            s.add_launch("notepad.exe");
            s.add_focus("notepad");
            s.add_delay(50);
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("save_file");
            s.add_chord(VK_CONTROL, 'S');
            s.add_delay(500);
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("new_file");
            s.add_chord(VK_CONTROL, 'N');
            s.add_delay(300);
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("select_all");
            s.add_chord(VK_CONTROL, 'A');
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("copy");
            s.add_chord(VK_CONTROL, 'C');
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("paste");
            s.add_chord(VK_CONTROL, 'V');
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("undo");
            s.add_chord(VK_CONTROL, 'Z');
            register_procedure(s);
        }
        {
            MotorSequence s;
            s.init("close_window");
            s.add_chord(VK_MENU, VK_F4);
            register_procedure(s);
        }

        printf("  [MotorCortex] %d built-in procedures loaded.\n", procedure_count);
    }
};
