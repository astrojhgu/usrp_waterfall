#include "data_proc.hpp"
#include <fftw3.h>
#include <atomic>
#include "utils.hpp"

void waterfall(BufQ<DataFrame>& bufq, size_t nch, size_t batch, std::atomic_bool& stop_signal_called, std::function<void(const DataFrame&)> handler){
    stop_signal_called=false;
    std::cerr<<stop_signal_called<<std::endl;
    int n[]={(int)nch};
    int howmany=(int)batch;
    int idist=(int)nch;
    int odist=(int)nch;
    int istride=1;
    int ostride=1;
    int* inembed=n;
    int* onembed=n;
    int rank=1;
    fftwf_plan plan=fftwf_plan_many_dft(rank, n, howmany,
                            nullptr, inembed,
                            istride, idist,
                            nullptr, onembed,
                            ostride, odist,
                            FFTW_FORWARD, FFTW_ESTIMATE);

    size_t prev_cnt=0;

    for(int i=0;!stop_signal_called;++i){
        auto data=bufq.fetch();
        if (data->count>prev_cnt+1){
            std::cerr<<"Dropping packet"<<std::endl;
        }
        
        prev_cnt=data->count;
        fftwf_execute_dft(plan, (fftwf_complex*)data->payload.data(), (fftwf_complex*)data->payload.data());
        fft_shift(data->payload, nch, batch);
        std::cerr<<i<<" "<<data->count<<std::endl;
        handler(*data);
    }
    fftwf_destroy_plan(plan);
}
