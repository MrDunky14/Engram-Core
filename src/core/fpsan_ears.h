#pragma once
// ============================================================
// FP-SAN Phase 20: EARS — Acoustic Cortex
// fpsan_ears.h — Native SAPI continuous dictation on a dedicated thread.
//
// This cortex listens in passive mode for wake words and, once primed,
// forwards recognized speech to the main loop through a lock-free SPSC queue.
// ============================================================

#include <windows.h>
#include <sapi.h>

#include <atomic>
#include <thread>
#include <string>
#include <cstring>
#include <cctype>
#include <vector>
#include <complex>
#include <mutex>
#include <mmsystem.h>
#include <cmath>
#include <fstream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

struct AcousticCommand {
    char text[512];
    bool wake_triggered;
};

struct AcousticQueue {
    static const int CAP = 64;
    AcousticCommand buffer[CAP];
    std::atomic<int> head;
    std::atomic<int> tail;

    void init() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    bool push(const AcousticCommand& cmd) {
        int h = head.load(std::memory_order_relaxed);
        int n = (h + 1) % CAP;
        int t = tail.load(std::memory_order_acquire);
        if (n == t) return false;
        buffer[h] = cmd;
        head.store(n, std::memory_order_release);
        return true;
    }

    bool pop(AcousticCommand& out) {
        int t = tail.load(std::memory_order_relaxed);
        int h = head.load(std::memory_order_acquire);
        if (t == h) return false;
        out = buffer[t];
        tail.store((t + 1) % CAP, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return tail.load(std::memory_order_acquire) == head.load(std::memory_order_acquire);
    }
};

struct EarsCortex {
    std::thread acoustic_thread;
    std::atomic<bool> thread_running;
    std::atomic<bool> initialized;
    std::atomic<bool> enabled;

    // Stage state:
    // passive -> wait for wake word
    // active  -> accept command dictation for a short time window
    std::atomic<bool> active_mode;
    std::atomic<ULONGLONG> active_until_ms;

    // Lightweight wake metrics
    std::atomic<float> last_energy;
    std::atomic<bool> last_wake_hit;
    
    // Initialization diagnostics (so the REPL can explain why speech is dead)
    enum InitError : int {
        ERR_NONE = 0,
        ERR_COM_INIT = 1,
        ERR_CREATE_RECOGNIZER = 2,
        ERR_SET_INPUT = 3,
        ERR_CREATE_CONTEXT = 4,
        ERR_CREATE_GRAMMAR = 5,
        ERR_LOAD_DICTATION = 6
    };
    std::atomic<int> init_error;

    AcousticQueue queue;

    void init() {
        queue.init();
        thread_running.store(true, std::memory_order_relaxed);
        initialized.store(false, std::memory_order_relaxed);
        enabled.store(true, std::memory_order_relaxed);
        active_mode.store(false, std::memory_order_relaxed);
        active_until_ms.store(0, std::memory_order_relaxed);
        last_energy.store(0.0f, std::memory_order_relaxed);
        last_wake_hit.store(false, std::memory_order_relaxed);
        init_error.store(ERR_NONE, std::memory_order_relaxed);

        // Audio capture ring for MFCC/VAD front-gate
        audio_init();

        acoustic_thread = std::thread(&EarsCortex::acoustic_loop, this);
    }

    void shutdown() {
        thread_running.store(false, std::memory_order_release);
        audio_shutdown();
        if (acoustic_thread.joinable()) {
            acoustic_thread.join();
        }
    }

    void toggle() {
        bool now_enabled = !enabled.load(std::memory_order_acquire);
        enabled.store(now_enabled, std::memory_order_release);
    }

    bool is_enabled() const {
        return enabled.load(std::memory_order_acquire);
    }

    bool is_initialized() const {
        return initialized.load(std::memory_order_acquire);
    }

    const char* last_error_string() const {
        switch ((InitError)init_error.load(std::memory_order_acquire)) {
            case ERR_NONE: return "none";
            case ERR_COM_INIT: return "COM init failed";
            case ERR_CREATE_RECOGNIZER: return "CoCreateInstance recognizer failed (SAPI missing?)";
            case ERR_SET_INPUT: return "SetInput failed (no default microphone / permission)";
            case ERR_CREATE_CONTEXT: return "CreateRecoContext failed";
            case ERR_CREATE_GRAMMAR: return "CreateGrammar failed";
            case ERR_LOAD_DICTATION: return "LoadDictation failed (speech components missing)";
            default: return "unknown";
        }
    }

    // Debug escape hatch: force active listening window without wake word.
    void force_listen_ms(int ms = 8000) {
        active_mode.store(true, std::memory_order_release);
        active_until_ms.store(GetTickCount64() + (ULONGLONG)ms, std::memory_order_release);
    }

    bool pop_command(AcousticCommand& out) {
        return queue.pop(out);
    }

private:
    // --- Audio capture for MFCC front-gate ---
    static const int SAMPLE_RATE = 16000;
    static const int RING_SECONDS = 2; // keep last 2s
    std::vector<int16_t> audio_ring;
    std::atomic<int> audio_write_pos {0};
    CRITICAL_SECTION audio_lock;
    HWAVEIN hWaveIn = nullptr;
    std::vector<WAVEHDR> wave_headers;
    bool audio_capturing = false;

    // MFCC baseline profile
    std::vector<float> mfcc_baseline;
    float mfcc_threshold = 40.0f; // euclidean threshold, adjustable

    static void CALLBACK wave_in_callback(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (uMsg != WIM_DATA) return;
        EarsCortex* self = reinterpret_cast<EarsCortex*>(dwInstance);
        WAVEHDR* hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
        if (!hdr || !self) return;

        int16_t* samples = reinterpret_cast<int16_t*>(hdr->lpData);
        int nSamples = hdr->dwBytesRecorded / sizeof(int16_t);

        EnterCriticalSection(&self->audio_lock);
        for (int i = 0; i < nSamples; i++) {
            int pos = self->audio_write_pos.load(std::memory_order_relaxed);
            self->audio_ring[pos] = samples[i];
            pos++; if (pos >= (int)self->audio_ring.size()) pos = 0;
            self->audio_write_pos.store(pos, std::memory_order_relaxed);
        }
        LeaveCriticalSection(&self->audio_lock);

        waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR));
    }

    void audio_init() {
        audio_ring.resize(SAMPLE_RATE * RING_SECONDS);
        InitializeCriticalSection(&audio_lock);
        audio_write_pos.store(0);
        // Prepare waveIn
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = SAMPLE_RATE;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = (fmt.wBitsPerSample / 8) * fmt.nChannels;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

        MMRESULT mr = waveInOpen(&hWaveIn, WAVE_MAPPER, &fmt, (DWORD_PTR)wave_in_callback, (DWORD_PTR)this, CALLBACK_FUNCTION);
        if (mr != MMSYSERR_NOERROR) {
            hWaveIn = nullptr;
            printf("[AcousticCortex] ERROR: waveInOpen failed (MMRESULT %d). Microphone not accessible.\n", mr);
            return;
        }

        const int BUF_MS = 100;
        int bufSamples = SAMPLE_RATE * BUF_MS / 1000;
        int bufBytes = bufSamples * sizeof(int16_t);
        const int NBUF = 16;
        wave_headers.resize(NBUF);
        for (int i = 0; i < NBUF; i++) {
            wave_headers[i].lpData = (LPSTR)malloc(bufBytes);
            wave_headers[i].dwBufferLength = bufBytes;
            wave_headers[i].dwBytesRecorded = 0;
            wave_headers[i].dwFlags = 0;
            waveInPrepareHeader(hWaveIn, &wave_headers[i], sizeof(WAVEHDR));
            waveInAddBuffer(hWaveIn, &wave_headers[i], sizeof(WAVEHDR));
        }
        waveInStart(hWaveIn);
        audio_capturing = true;

        load_mfcc_baseline("artefacts/ear_profile.bin");
        if (!mfcc_baseline.empty()) {
            printf("[MFCC] Profile loaded (%zu coefficients).\n", mfcc_baseline.size());
        } else {
            printf("[MFCC] No baseline profile found. Using SAPI confidence gate until calibration.\n");
        }
        printf("[AcousticCortex] Initialized. 16kHz ring buffer online.\n");
    }

    void audio_shutdown() {
        if (!audio_capturing) return;
        audio_capturing = false;
        if (hWaveIn) {
            waveInStop(hWaveIn);
            waveInReset(hWaveIn);
            for (auto &hdr : wave_headers) {
                waveInUnprepareHeader(hWaveIn, &hdr, sizeof(WAVEHDR));
                if (hdr.lpData) free(hdr.lpData);
            }
            wave_headers.clear();
            waveInClose(hWaveIn);
            hWaveIn = nullptr;
        }
        DeleteCriticalSection(&audio_lock);
    }

    // Get most recent samples up to 'ms' milliseconds. Returns samples length in out vector.
    int get_recent_samples(int ms, std::vector<int16_t>& out) {
        int want = SAMPLE_RATE * ms / 1000;
        if (want <= 0) return 0;
        out.resize(want);
        EnterCriticalSection(&audio_lock);
        int write = audio_write_pos.load(std::memory_order_relaxed);
        int ringN = (int)audio_ring.size();
        int start = write - want;
        if (start < 0) start += ringN;
        for (int i = 0; i < want; i++) {
            int idx = start + i;
            if (idx >= ringN) idx -= ringN;
            out[i] = audio_ring[idx];
        }
        LeaveCriticalSection(&audio_lock);
        return want;
    }

    // --- Simple MFCC implementation (small, no external deps) ---
    static void fft(std::vector<std::complex<double>>& a) {
        int n = (int)a.size();
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            double ang = 2 * M_PI / len;
            std::complex<double> wlen(cos(ang), sin(ang));
            for (int i = 0; i < n; i += len) {
                std::complex<double> w(1);
                for (int j = 0; j < len/2; ++j) {
                    std::complex<double> u = a[i+j];
                    std::complex<double> v = a[i+j+len/2] * w;
                    a[i+j] = u + v;
                    a[i+j+len/2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    static double hz_to_mel(double hz) { return 2595.0 * log10(1.0 + hz / 700.0); }
    static double mel_to_hz(double mel) { return 700.0 * (pow(10.0, mel / 2595.0) - 1.0); }

    void compute_mfcc(const int16_t* samples, int n, std::vector<float>& out_mfcc) {
        if (n <= 0) { out_mfcc.clear(); return; }
        int NFFT = 1; while (NFFT < n) NFFT <<= 1; if (NFFT < 512) NFFT = 512;
        std::vector<std::complex<double>> x(NFFT);
        for (int i = 0; i < n; ++i) x[i] = std::complex<double>(samples[i] * 0.5, 0.0);
        for (int i = n; i < NFFT; ++i) x[i] = 0.0;
        fft(x);
        int nfft2 = NFFT/2 + 1;
        std::vector<double> power(nfft2);
        for (int i = 0; i < nfft2; ++i) power[i] = std::norm(x[i]);

        int nfilt = 26;
        int ncoeff = 13;
        double low_mel = hz_to_mel(0);
        double high_mel = hz_to_mel(SAMPLE_RATE/2);
        std::vector<double> mel_points(nfilt + 2);
        for (int i = 0; i < nfilt + 2; ++i) mel_points[i] = low_mel + (high_mel - low_mel) * i / (nfilt + 1);
        std::vector<int> bins(nfilt + 2);
        for (int i = 0; i < nfilt + 2; ++i) bins[i] = (int)floor((NFFT + 1) * mel_to_hz(mel_points[i]) / SAMPLE_RATE);

        std::vector<double> filt_energy(nfilt);
        for (int m = 1; m <= nfilt; ++m) {
            int f_m_minus = bins[m-1];
            int f_m = bins[m];
            int f_m_plus = bins[m+1];
            double sum = 0.0;
            for (int k = f_m_minus; k < f_m; ++k) if (k >= 0 && k < nfft2) sum += (double)(k - f_m_minus) / (f_m - f_m_minus + 1e-12) * power[k];
            for (int k = f_m; k < f_m_plus; ++k) if (k >= 0 && k < nfft2) sum += (double)(f_m_plus - k) / (f_m_plus - f_m + 1e-12) * power[k];
            filt_energy[m-1] = sum > 1e-12 ? log(sum) : -50.0;
        }

        // DCT-II
        out_mfcc.assign(ncoeff, 0.0f);
        for (int k = 0; k < ncoeff; ++k) {
            double s = 0.0;
            for (int n2 = 0; n2 < nfilt; ++n2) s += filt_energy[n2] * cos(M_PI * k * (2*n2 + 1) / (2.0 * nfilt));
            out_mfcc[k] = (float)s;
        }
    }

    // Compute MFCC for recent ms and compare to baseline. Returns true if matches (i.e., user).
    bool verify_wake_mfcc(int recent_ms) {
        if (mfcc_baseline.empty()) return true; // no baseline = accept
        std::vector<int16_t> samples;
        int got = get_recent_samples(recent_ms, samples);
        if (got <= 0) return true;
        std::vector<float> mfcc;
        compute_mfcc(samples.data(), got, mfcc);
        if (mfcc.size() != mfcc_baseline.size()) return true;
        double sum = 0.0;
        for (size_t i = 0; i < mfcc.size(); ++i) {
            double d = mfcc[i] - mfcc_baseline[i];
            sum += d * d;
        }
        double dist = sqrt(sum);
        return dist <= mfcc_threshold;
    }

    void save_mfcc_baseline(const char* path) {
        if (mfcc_baseline.empty()) return;
        std::ofstream f(path, std::ios::binary);
        if (!f) return;
        int n = (int)mfcc_baseline.size();
        f.write((char*)&n, sizeof(n));
        f.write((char*)mfcc_baseline.data(), n * sizeof(float));
    }

    void load_mfcc_baseline(const char* path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return;
        int n = 0; f.read((char*)&n, sizeof(n));
        if (n <= 0) return;
        mfcc_baseline.resize(n);
        f.read((char*)mfcc_baseline.data(), n * sizeof(float));
    }

    void start_calibration(int captures = 5, int ms = 800) {
        std::vector<float> accum;
        for (int i = 0; i < captures; ++i) {
            // brief pause to allow user to speak
            Sleep(700);
            std::vector<int16_t> s; get_recent_samples(ms, s);
            std::vector<float> mf; compute_mfcc(s.data(), (int)s.size(), mf);
            if (mf.empty()) continue;
            if (accum.empty()) accum.assign(mf.size(), 0.0f);
            for (size_t k = 0; k < mf.size(); ++k) accum[k] += mf[k];
        }
        if (!accum.empty()) {
            for (float &v : accum) v /= (float)captures;
            mfcc_baseline.assign(accum.begin(), accum.end());
            save_mfcc_baseline("artefacts/ear_profile.bin");
        }
    }

    static std::string lower_copy(const std::string& s) {
        std::string out = s;
        for (char& c : out) c = (char)tolower((unsigned char)c);
        return out;
    }

    static bool contains_ci(const std::string& hay, const char* needle) {
        if (!needle) return false;
        std::string h = lower_copy(hay);
        std::string n = lower_copy(std::string(needle));
        return h.find(n) != std::string::npos;
    }

    static std::string trim(const std::string& s) {
        size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r' || s[a] == ',')) a++;
        size_t b = s.size();
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
        return s.substr(a, b - a);
    }

    bool extract_wake_remainder(const std::string& spoken, std::string& remainder) {
        std::string low = lower_copy(spoken);
        const char* wakes[] = { "hey jarvis", "hey buddy" };
        for (const char* w : wakes) {
            std::string wk = w;
            size_t pos = low.find(wk);
            if (pos != std::string::npos) {
                size_t start = pos + wk.size();
                if (start < spoken.size()) {
                    remainder = trim(spoken.substr(start));
                } else {
                    remainder.clear();
                }
                return true;
            }
        }
        return false;
    }

    bool looks_like_command(const std::string& s) {
        std::string l = lower_copy(trim(s));
        return !l.empty(); // Phase 19: All speech is passed to the Natural Language Intent Parser
    }

    void enqueue_text(const std::string& text, bool wake_hit) {
        std::string t = trim(text);
        if (t.empty()) return;

        AcousticCommand c{};
        c.wake_triggered = wake_hit;
        strncpy(c.text, t.c_str(), sizeof(c.text) - 1);
        c.text[sizeof(c.text) - 1] = '\0';
        queue.push(c);
    }

    void on_recognized(ISpRecoResult* result) {
        if (!result) return;

        WCHAR* wtext = nullptr;
        HRESULT hr = result->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &wtext, nullptr);
        if (FAILED(hr) || !wtext) return;

        char text_utf8[1024] = {0};
        WideCharToMultiByte(CP_UTF8, 0, wtext, -1, text_utf8, (int)sizeof(text_utf8), nullptr, nullptr);
        ::CoTaskMemFree(wtext);

        std::string spoken = trim(std::string(text_utf8));
        if (spoken.empty()) return;

        SPPHRASE* phrase = nullptr;
        float confidence = 0.0f;
        if (SUCCEEDED(result->GetPhrase(&phrase)) && phrase) {
            if (phrase->Rule.ulCountOfElements > 0 && phrase->pElements) {
                float sum = 0.0f;
                ULONG n = phrase->Rule.ulCountOfElements;
                for (ULONG i = 0; i < n; i++) {
                    sum += phrase->pElements[i].SREngineConfidence;
                }
                confidence = sum / (float)n;
            }
            ::CoTaskMemFree(phrase);
        }
        last_energy.store(confidence, std::memory_order_release);

        if (!enabled.load(std::memory_order_acquire)) return;

        std::string remainder;
        bool wake_hit = extract_wake_remainder(spoken, remainder);
        if (wake_hit && confidence >= -0.45f) {
            // Verify candidate wake with MFCC front-gate (if baseline exists)
            bool mfcc_ok = verify_wake_mfcc(800);
            if (!mfcc_ok) {
                // Treat as false positive
                last_wake_hit.store(false, std::memory_order_release);
                return;
            }

            last_wake_hit.store(true, std::memory_order_release);
            active_mode.store(true, std::memory_order_release);
            active_until_ms.store(GetTickCount64() + 8000ULL, std::memory_order_release);

            // Check for special calibration phrase
            std::string low = lower_copy(spoken);
            if (low == "calibrate wake" || low == "calibrate ear" || low == "calibrate ears") {
                // Run calibration in background
                std::thread([this]{
                    printf("[Ears] Starting MFCC calibration. Please say the wake phrase 5 times.");
                    start_calibration(5, 800);
                    printf("\n[Ears] Calibration complete. Baseline saved.\n");
                }).detach();
                return;
            }

            if (!remainder.empty() && looks_like_command(remainder)) {
                enqueue_text(remainder, true);
            }
            return;
        }

        if (active_mode.load(std::memory_order_acquire)) {
            ULONGLONG now = GetTickCount64();
            ULONGLONG until = active_until_ms.load(std::memory_order_acquire);
            if (now > until) {
                active_mode.store(false, std::memory_order_release);
                return;
            }

            if (looks_like_command(spoken)) {
                enqueue_text(spoken, false);
                active_until_ms.store(now + 4000ULL, std::memory_order_release);
            }
        }
    }

    void acoustic_loop() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            fprintf(stderr, "[Ears] COM initialization failed.\n");
            init_error.store(ERR_COM_INIT, std::memory_order_release);
            initialized.store(false, std::memory_order_release);
            return;
        }

        ISpRecognizer* recognizer = nullptr;
        ISpRecoContext* context = nullptr;
        ISpRecoGrammar* grammar = nullptr;

        hr = CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ISpRecognizer, (void**)&recognizer);
        if (FAILED(hr) || !recognizer) {
            fprintf(stderr, "[Ears] Failed to create recognizer.\n");
            init_error.store(ERR_CREATE_RECOGNIZER, std::memory_order_release);
            CoUninitialize();
            initialized.store(false, std::memory_order_release);
            return;
        }

        hr = recognizer->SetInput(nullptr, TRUE);
        if (FAILED(hr)) {
            fprintf(stderr, "[Ears] Failed to bind default audio input.\n");
            init_error.store(ERR_SET_INPUT, std::memory_order_release);
            recognizer->Release();
            CoUninitialize();
            initialized.store(false, std::memory_order_release);
            return;
        }

        hr = recognizer->CreateRecoContext(&context);
        if (FAILED(hr) || !context) {
            fprintf(stderr, "[Ears] Failed to create recognition context.\n");
            init_error.store(ERR_CREATE_CONTEXT, std::memory_order_release);
            recognizer->Release();
            CoUninitialize();
            initialized.store(false, std::memory_order_release);
            return;
        }

        context->SetNotifyWin32Event();
        context->SetInterest(SPFEI(SPEI_RECOGNITION), SPFEI(SPEI_RECOGNITION));

        hr = context->CreateGrammar(1, &grammar);
        if (FAILED(hr) || !grammar) {
            fprintf(stderr, "[Ears] Failed to create dictation grammar.\n");
            init_error.store(ERR_CREATE_GRAMMAR, std::memory_order_release);
            context->Release();
            recognizer->Release();
            CoUninitialize();
            initialized.store(false, std::memory_order_release);
            return;
        }

        hr = grammar->LoadDictation(nullptr, SPLO_STATIC);
        if (FAILED(hr)) {
            fprintf(stderr, "[Ears] Failed to load dictation grammar.\n");
            init_error.store(ERR_LOAD_DICTATION, std::memory_order_release);
            grammar->Release();
            context->Release();
            recognizer->Release();
            CoUninitialize();
            initialized.store(false, std::memory_order_release);
            return;
        }

        grammar->SetDictationState(SPRS_ACTIVE);
        init_error.store(ERR_NONE, std::memory_order_release);
        initialized.store(true, std::memory_order_release);
        printf("[Ears] Acoustic Cortex online. Listening for wake word.\n");

        HANDLE evt = context->GetNotifyEventHandle();
        while (thread_running.load(std::memory_order_acquire)) {
            DWORD wr = WaitForSingleObject(evt, 50);
            if (wr == WAIT_OBJECT_0) {
                SPEVENT ev;
                ULONG fetched = 0;
                while (SUCCEEDED(context->GetEvents(1, &ev, &fetched)) && fetched > 0) {
                    if (ev.eEventId == SPEI_RECOGNITION) {
                        ISpRecoResult* result = reinterpret_cast<ISpRecoResult*>(ev.lParam);
                        if (result) {
                            on_recognized(result);
                            result->Release();
                        }
                    }
                }
            }

            if (active_mode.load(std::memory_order_acquire)) {
                ULONGLONG now = GetTickCount64();
                ULONGLONG until = active_until_ms.load(std::memory_order_acquire);
                if (now > until) {
                    active_mode.store(false, std::memory_order_release);
                }
            }
        }

        grammar->SetDictationState(SPRS_INACTIVE);
        grammar->Release();
        context->Release();
        recognizer->Release();

        initialized.store(false, std::memory_order_release);
        CoUninitialize();
        printf("[Ears] Acoustic Cortex offline.\n");
    }

public:
    // ── Phase 4: Spike phoneme nodes from MFCC feature vector ──
    // Maps MFCC bin i → graph node (phoneme_base_id + i).
    // Voltage = mfcc_bins[i] / 100.0f (normalized to [0,1] range).
    // Caller must hold shared_lock on graph_rw_lock.
    void spike_phoneme_nodes(const float* mfcc_bins, int n_bins,
                             ClusterGraph* graph, int phoneme_base_id) noexcept {
        if (!graph || phoneme_base_id < 0 || n_bins <= 0) return;
        const int nc = graph->node_count.load(std::memory_order_acquire);
        for (int i = 0; i < n_bins; i++) {
            int nid = phoneme_base_id + i;
            if (nid >= nc) break;
            if (!graph->node(nid).alive.load(std::memory_order_relaxed)) continue;
            float v = mfcc_bins[i] * 0.01f;
            if (v > 0.01f) graph->node(nid).add_voltage(v);
        }
    }

    // Convenience: extract MFCC from the last 25ms window and spike nodes.
    // Returns number of nodes spiked.
    int tick_mfcc_spike(ClusterGraph* graph, int phoneme_base_id, int n_bins = 13) noexcept {
        if (!audio_capturing || !graph) return 0;
        std::vector<int16_t> samples;
        int got = get_recent_samples(25, samples);
        if (got < 128) return 0;
        std::vector<float> mfcc;
        compute_mfcc(samples.data(), (int)samples.size(), mfcc);
        if (mfcc.empty()) return 0;
        int nb = (n_bins < (int)mfcc.size()) ? n_bins : (int)mfcc.size();
        spike_phoneme_nodes(mfcc.data(), nb, graph, phoneme_base_id);
        return nb;
    }
};
