#ifndef DATA_PROC
#define DATA_PROC
#include <functional>
#include "bufq.hpp"
#include "daq_queue.hpp"

void waterfall(BufQ<DataFrame>& bufq, size_t nch, size_t batch, std::atomic_bool& stop_signal_called, std::function<void(const DataFrame&)> handler);
#endif
