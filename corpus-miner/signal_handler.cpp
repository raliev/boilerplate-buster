#include "signal_handler.h"
#include <iostream>

std::atomic<bool> g_stop_requested{false};

void signal_handler(int signum) {
    if (signum == SIGINT) {
        std::cout << "\n[!] Interrupt signal received (Ctrl+C). Finishing current phrase and saving..." << std::endl;
        g_stop_requested = true;
    }
}
