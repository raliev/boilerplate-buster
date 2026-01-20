#ifndef TIMER_H
#define TIMER_H

#include <chrono>
#include <string>
#include <iostream>

inline auto start_timer() {
    return std::chrono::high_resolution_clock::now();
}

inline void stop_timer(const std::string& label, std::chrono::high_resolution_clock::time_point start) {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "[TIMER] " << label << ": " << elapsed.count() << " seconds" << std::endl;
}

#endif // TIMER_H
