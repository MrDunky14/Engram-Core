// FP-SAN LIVE COGNITIVE CORE
// Receives binarized 28x28 sensor frames via UDP and runs the cognitive loop.
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\core\fpsan_live_core.cpp /Fe:build\live_core.exe ws2_32.lib

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "fpsan_core.h"
#include "sensor_bus.h"
#include "fpsan_motor.h"
#include "fpsan_sequencer.h"

using namespace std;
using namespace std::chrono;

const int UDP_PORT_VISION = 5005;
const int UDP_PORT_LANGUAGE = 5006;
const int FRAME_SIZE = CORE_INPUT_DIM; // 784 bytes

// The Ring Buffer (100 frames deep)
SensorBus<std::vector<int8_t>, 100> sensor_bus;

bool running = true;
bool test_mode = true; // Set to false to allow actual Windows Motor control (dangerous!)

// ============================================================
// SENSOR THREAD (UDP Listener)
// ============================================================
void sensor_thread_func(int port, SensorBus<std::vector<int8_t>, 100>* bus, bool is_language) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "[Sensor] WSAStartup failed.\n";
        return;
    }

    SOCKET udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket == INVALID_SOCKET) {
        cerr << "[Sensor] Socket creation failed.\n";
        WSACleanup();
        return;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "[Sensor] Bind failed on port " << port << ".\n";
        closesocket(udp_socket);
        WSACleanup();
        return;
    }

    cout << "[Sensor] Listening for UDP frames on port " << port << "...\n";

    char recv_buf[2048];
    while (running) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int bytes_received = recvfrom(udp_socket, recv_buf, sizeof(recv_buf), 0, (sockaddr*)&client_addr, &client_len);

        if (bytes_received > 0) {
            if (is_language) {
                // Read as byte string
                std::vector<int8_t> frame(bytes_received);
                memcpy(frame.data(), recv_buf, bytes_received);
                bus->push(frame);
            } else if (bytes_received == FRAME_SIZE) {
                // Read as binary visual frame
                std::vector<int8_t> frame(FRAME_SIZE);
                for (int i = 0; i < FRAME_SIZE; i++) {
                    frame[i] = (recv_buf[i] > 0 && recv_buf[i] != '0') ? 1 : 0;
                }
                bus->push(frame);
            }
        }
    }

    closesocket(udp_socket);
    WSACleanup();
}

// ============================================================
// COGNITIVE LOOP (Main Thread)
// ============================================================
SensorBus<std::vector<int8_t>, 100> vision_bus;
SensorBus<std::vector<int8_t>, 100> language_bus;

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN LIVE COGNITIVE CORE" << endl;
    cout << " SENSE -> PERCEIVE -> REMEMBER -> PREDICT -> REASON" << endl;
    cout << "================================================================\n" << endl;

    CognitiveCore* brain = new CognitiveCore();
    brain->boot(true);

    Sequencer sequencer;
    sequencer.init(&(brain->graph));

    MotorCortex motor;
    motor.init();
    
    SpikingTokenizer tokenizer;

    // Start UDP sensor listeners
    thread vision_thread(sensor_thread_func, UDP_PORT_VISION, &vision_bus, false);
    thread language_thread(sensor_thread_func, UDP_PORT_LANGUAGE, &language_bus, true);

    // Benchmarking metrics
    uint64_t frames_processed = 0;
    auto start_time = high_resolution_clock::now();
    auto last_report = start_time;
    double total_latency_us = 0;

    int previous_active_clusters = 0;

    vector<int8_t> current_frame;

    cout << "[Core] Entering Cognitive Nursery Loop...\n" << endl;

    while (running) {
        bool processed_vision = false;

        // 1A. Process Language Input (if any)
        vector<int8_t> lang_str_data;
        if (language_bus.pop(lang_str_data)) {
            // Convert byte array back to string
            string word(lang_str_data.begin(), lang_str_data.end());
            
            // Hash into 256-dim spike array
            int8_t word_hash[LANG_WORD_DIM];
            tokenizer.encode_word_hash(word, word_hash);
            
            // Perceive via Language Cortex
            int lang_cluster = brain->language.perceive(word_hash, true);
            
            if (lang_cluster >= 0) {
                int global_id = 1000 + lang_cluster;

                if (word.length() > 0 && word[0] == '!') {
                    // Command: SET PERSISTENT GOAL (e.g. !OPEN_NOTEPAD)
                    sequencer.set_goal(global_id);
                    cout << "\n[JARVIS] Goal Set: " << word << endl;
                } 
                else if (word.length() > 0 && word[0] == '?') {
                    // Command: QUERY (e.g. ?NOTEPAD)
                    brain->graph.clear_activation();
                    brain->graph.spread_activation(global_id);
                    cout << "\n[JARVIS] Query Executed: " << word << endl;
                }
                else {
                    // Context: OBSERVATIONAL (Associative Learning)
                    brain->graph.record_fire(global_id);
                }
            }
        }

        // 1B. Process Vision Input
        if (vision_bus.pop(current_frame)) {
            processed_vision = true;
            auto t0 = high_resolution_clock::now();

            // 2. Run cognitive cycle (learning enabled)
            // Instead of standard tick, we use the Sequencer for Working Memory management
            brain->tick_count++;
            brain->current_cluster = brain->perception.perceive(current_frame.data(), true);
            
            if (brain->current_cluster >= 0) {
                brain->graph.record_fire(brain->current_cluster);
            }

            // Detect anomalies (e.g., Pop-up) and inject sensory spike
            // For this simulation, if we see cluster 50 (arbitrary Pop-up ID), spike it!
            int anomaly_spike = (brain->current_cluster == 50) ? brain->current_cluster : -1;
            
            // Temporal Tick (Spreads voltage from clamped goals, injects anomalies, decays memory)
            sequencer.tick(anomaly_spike, 1.5f);

            // 3. Curiosity Dopamine
            // If the brain's action caused a NEW cluster to form on the screen, reward it!
            int current_clusters = brain->perception.active_count();
            if (current_clusters > previous_active_clusters) {
                // Discovery! Give dopamine to strengthen the thought sequence
                brain->reward(1.0f);
                previous_active_clusters = current_clusters;
            } else {
                // Boredom penalty: if we do something and nothing new happens, 
                // slowly penalize the action so the Q-value drops and we try something else.
                brain->reward(-0.01f);
            }

            // 4. Action Selection via Working Memory Sequencer
            int action_candidates[] = {1, 2, 3, 4, 5, 6, 7, 8}; 
            int action = sequencer.select_action(action_candidates, 8);
            
            if (action >= 0) {
                if (!test_mode) {
                    motor.execute(action);
                } else {
                    cout << "\r[TEST_MODE] JARVIS suppressed motor action: " << action << "          " << flush;
                }
            }

            auto t1 = high_resolution_clock::now();
            total_latency_us += duration_cast<nanoseconds>(t1 - t0).count() / 1000.0;
            frames_processed++;

            // Periodically report stats (every ~100 frames)
            if (frames_processed % 100 == 0) {
                auto now = high_resolution_clock::now();
                double elapsed_sec = duration_cast<milliseconds>(now - last_report).count() / 1000.0;
                double fps = 100.0 / elapsed_sec;
                double avg_lat = total_latency_us / 100.0;

                cout << "\r[Tick " << setw(6) << brain->tick_count << "] "
                     << "Clusters: " << setw(3) << current_clusters << " | "
                     << "Exploration: " << fixed << setprecision(2) << brain->agency.exploration_rate << " | "
                     << "Pred: " << setw(3) << brain->predicted_next << " | "
                     << "Act: " << action << " | "
                     << "Lat: " << fixed << setprecision(1) << avg_lat << " us          " << flush;

                last_report = now;
                total_latency_us = 0;
            }
        } else {
            // Idle — brain could replay memories, consolidate, etc.
            // For now, sleep slightly to prevent 100% CPU on empty bus
            this_thread::sleep_for(milliseconds(1));
        }
    }

    running = false;
    vision_thread.join();
    language_thread.join();
    return 0;
}
