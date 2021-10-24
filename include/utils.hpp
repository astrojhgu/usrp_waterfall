#ifndef UTILS_HPP
#define UTILS_HPP
#include <cassert>
#include <string>
#include <complex>
#include <vector>
#include <tuple>
#include "config.hpp"



template <typename A>
void fft_shift(A& data, size_t nch, size_t batch){
    for(int i=0;i<batch;++i){
        for(int j=0;j<nch/2;++j){
            std::swap(data[i*nch+j], data[i*nch+j+nch/2]);
        }
    }
}


std::tuple<float,float, std::vector<SAMP_TYPE>> minmax(const std::vector<std::complex<SAMP_TYPE>>& data);

#endif
