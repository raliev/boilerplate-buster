#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <atomic>
#include <csignal>

extern std::atomic<bool> g_stop_requested;

void signal_handler(int signum);

#endif // SIGNAL_HANDLER_H
