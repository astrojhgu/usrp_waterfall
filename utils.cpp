#include <cassert>
#include <string>
#include <complex>
#include <vector>
#include <tuple>
#include "config.hpp"


std::tuple<float,float, std::vector<SAMP_TYPE>> minmax(const std::vector<std::complex<SAMP_TYPE>>& data){
    std::vector<SAMP_TYPE> ampl(data.size());
    SAMP_TYPE min_value=1e99;
    SAMP_TYPE max_value=-1e99;
    for(int i=0;i<data.size();++i){
        
        ampl[i]=std::norm(data[i]);
        auto x=ampl[i];
        if (min_value>x){
            min_value=x;
        }
        if (max_value<x){
            max_value=x;
        }
    }
    return std::make_tuple(min_value, max_value, ampl);
}
