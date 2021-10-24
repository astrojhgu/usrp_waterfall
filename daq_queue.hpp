#ifndef DAQ_QUEUE_HPP
#define DAQ_QUEUE_HPP
#include <vector>
#include <complex>
#include "config.hpp"

struct DataFrame{
    std::size_t count;
    std::vector<std::complex<SAMP_TYPE>> payload;
    DataFrame(std::size_t c, std::vector<std::complex<SAMP_TYPE>>&& _payload);
};


#endif
