#include <daq_queue.hpp>

DataFrame::DataFrame(std::size_t c, std::vector<std::complex<SAMP_TYPE>>&& _payload)
:count(c), payload(_payload)
{}
