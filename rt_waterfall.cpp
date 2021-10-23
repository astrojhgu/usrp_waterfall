//
// Copyright 2010-2011,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <complex>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <fftw3.h>
#include <SDL2/SDL.h>
#include "bufq.hpp"



namespace po = boost::program_options;
using SAMP_TYPE=float;

template <typename T>
std::string type_code(){
    assert(0);
    return "";
}

template <typename A>
void fft_shift(A& data, size_t nch, size_t batch){
    for(int i=0;i<batch;++i){
        for(int j=0;j<nch/2;++j){
            std::swap(data[i*nch+j], data[i*nch+j+nch/2]);
        }
    }
}

template <>
std::string type_code<double>(){
    return "fc64";
}

template <>
std::string type_code<float>(){
    return "fc32";
}


std::uint32_t map_color(SAMP_TYPE x, SAMP_TYPE xmin, SAMP_TYPE xmax){
    auto y=x/xmax;
    uint32_t r=y*255;
    uint32_t g=y*255;
    uint32_t b=y*255;
    return (0xFF000000|(r<<16)|(g<<8)|b);
}


struct DataFrame{
    std::size_t count;
    std::vector<std::complex<SAMP_TYPE>> payload;

    DataFrame(std::size_t c, std::vector<std::complex<SAMP_TYPE>>&& _payload)
    :count(c), payload(_payload)
    {}
};


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



static std::atomic_bool stop_signal_called(false);
void sig_int_handler(int s)
{
    std::cerr<<s<<std::endl;
    if(s==15||s==9||s==2){
        stop_signal_called = true;
    }
    
}

void recv_and_proc(uhd::usrp::multi_usrp::sptr usrp,
    //const std::string& cpu_format,
    const std::string& wire_format,
    const size_t& channel,
    size_t nch,
    size_t batch
    )
{
    size_t samps_per_buff=nch*batch;

    BufQ<DataFrame> bufq{std::make_shared<DataFrame>(0, std::vector<std::complex<SAMP_TYPE>>(samps_per_buff)),
    std::make_shared<DataFrame>(0, std::vector<std::complex<SAMP_TYPE>>(samps_per_buff)),
    std::make_shared<DataFrame>(0, std::vector<std::complex<SAMP_TYPE>>(samps_per_buff))
    };

    

    unsigned long long num_total_samps = 0;
    // create a receive streamer
    uhd::stream_args_t stream_args(type_code<SAMP_TYPE>(), wire_format);
    std::vector<size_t> channel_nums;
    channel_nums.push_back(channel);
    stream_args.channels             = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    uhd::rx_metadata_t md;
    std::vector<std::complex<SAMP_TYPE>> buff(samps_per_buff);
    
    bool overflow_message = true;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps  = size_t(0);
    stream_cmd.stream_now = true;
    stream_cmd.time_spec  = uhd::time_spec_t();
    rx_stream->issue_stream_cmd(stream_cmd);

    const auto start_time = std::chrono::steady_clock::now();
    
    // Track time and samps between updating the BW summary
    auto last_update                     = start_time;
    unsigned long long last_update_samps = 0;

    // Run this loop until either time expired (if a duration was given), until
    // the requested number of samples were collected (if such a number was
    // given), or until Ctrl-C was pressed.

    
    std::thread th_proc([&]{
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
            return;
        }
        SDL_Window *window=SDL_CreateWindow("waterfall",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              800,600,
                              SDL_WINDOW_RESIZABLE);

        if (!window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create window: %s\n", SDL_GetError());
            return;
        }
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create renderer: %s\n", SDL_GetError());
            return;
        }

        SDL_Texture *waterfallTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, nch, batch);

        if (!waterfallTexture) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create texture: %s\n", SDL_GetError());
            return;
        }




        stop_signal_called=false;
        std::cerr<<stop_signal_called<<std::endl;
        int n[]={(int)nch};
        int howmany=batch;
        int idist=nch;
        int odist=nch;
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
            auto display_data=minmax(data->payload);

            std::cerr<<i<<" "<<data->count<<" "<<std::get<0>(display_data)<<" "<<std::get<1>(display_data)<<std::endl;
            SAMP_TYPE max_value=std::get<1>(display_data);
            SAMP_TYPE min_value=std::get<0>(display_data);
            std::vector<SAMP_TYPE>& dd=std::get<2>(display_data);

            //std::ofstream ofs("a.bin");
            //ofs.write((char*)data->payload.data(), sizeof(std::complex<float>)*data->payload.size());
            //ofs.close();
            SDL_Event event;

            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        stop_signal_called = true;
                    }
                    break;
                case SDL_QUIT:
                    stop_signal_called = true;
                    break;
                }
            }


            size_t idx=0;
            void *pixels;
            int pitch;
            if (SDL_LockTexture(waterfallTexture, NULL, &pixels, &pitch) < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't lock texture: %s\n", SDL_GetError());
                return;
            }

            for(int i=0;i<batch;++i){
                auto dst = (std::uint32_t*)((std::uint8_t*)pixels + i * pitch);
                for(int j=0;j<nch;++j){
                    std::uint32_t color=map_color(dd[idx++], min_value, max_value);
                    *dst++=color;
                }
            }
            SDL_UnlockTexture(waterfallTexture);

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, waterfallTexture, NULL, NULL);
            SDL_RenderPresent(renderer);
        }
        fftwf_destroy_plan(plan);
        SDL_DestroyRenderer(renderer);
    });


    std::thread th_acq([&]{
    for (size_t i=0;!stop_signal_called;++i) {
        const auto now = std::chrono::steady_clock::now();
        auto pbuf=bufq.prepare_write_buf();
        pbuf->count=i;

        size_t num_rx_samps =
            rx_stream->recv(&pbuf->payload.front(), pbuf->payload.size(), md, 1, false);
        bufq.submit();

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;

            stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
            rx_stream->issue_stream_cmd(stream_cmd);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            uhd::stream_cmd_t stream_cmd( uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            stream_cmd.num_samps  = 0;
            stream_cmd.stream_now = true;
            stream_cmd.time_spec  = uhd::time_spec_t();
            rx_stream->issue_stream_cmd(stream_cmd);


            continue;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            if (overflow_message) {
                overflow_message = false;
                std::cerr
                    << boost::format(
                           "Got an overflow indication. Please consider the following:\n"
                           "  Your write medium must sustain a rate of %fMB/s.\n"
                           "  Dropped samples will not be written to the file.\n"
                           "  Please modify this example for your purposes.\n"
                           "  This message will not appear again.\n")
                           % (usrp->get_rx_rate(channel) * sizeof(std::complex<SAMP_TYPE>) / 1e6);

                stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
                rx_stream->issue_stream_cmd(stream_cmd);

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
                stream_cmd.num_samps  = 0;
                stream_cmd.stream_now = true;
                stream_cmd.time_spec  = uhd::time_spec_t();
                rx_stream->issue_stream_cmd(stream_cmd);

            }
            continue;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
        }

        num_total_samps += num_rx_samps;

    }
    const auto actual_stop_time = std::chrono::steady_clock::now();

    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);
    });
    th_acq.join();
    th_proc.join();    
}

typedef std::function<uhd::sensor_value_t(const std::string&)> get_sensor_fn_t;

bool check_locked_sensor(std::vector<std::string> sensor_names,
    const char* sensor_name,
    get_sensor_fn_t get_sensor_fn,
    double setup_time)
{
    if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name)
        == sensor_names.end())
        return false;

    auto setup_timeout = std::chrono::steady_clock::now()
                         + std::chrono::milliseconds(int64_t(setup_time * 1000));
    bool lock_detected = false;

    std::cout << boost::format("Waiting for \"%s\": ") % sensor_name;
    std::cout.flush();

    while (true) {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout)) {
            std::cout << " locked." << std::endl;
            break;
        }
        if (get_sensor_fn(sensor_name).to_bool()) {
            std::cout << "+";
            std::cout.flush();
            lock_detected = true;
        } else {
            if (std::chrono::steady_clock::now() > setup_timeout) {
                std::cout << std::endl;
                throw std::runtime_error(
                    str(boost::format(
                            "timed out waiting for consecutive locks on sensor \"%s\"")
                        % sensor_name));
            }
            std::cout << "_";
            std::cout.flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << std::endl;
    return true;
}

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    // variables to be set by po
    std::string args, ant, subdev, ref, wirefmt;
    size_t channel, nch, batch;
    double rate, freq, gain, bw, setup_time, lo_offset;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("nch", po::value<size_t>(&nch)->default_value(1024), "num of chs")
        ("batch", po::value<size_t>(&batch)->default_value(32), "num of batches")
        ("rate", po::value<double>(&rate)->default_value(1e6), "rate of incoming samples")
        ("freq", po::value<double>(&freq)->default_value(0.0), "RF center frequency in Hz")
        ("lo-offset", po::value<double>(&lo_offset)->default_value(0.0),
            "Offset for frontend LO in Hz (optional)")
        ("gain", po::value<double>(&gain), "gain for the RF chain")
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("subdev", po::value<std::string>(&subdev), "subdevice specification")
        ("channel", po::value<size_t>(&channel)->default_value(0), "which channel to use")
        ("bw", po::value<double>(&bw), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (internal, external, mimo)")
        ("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc8"), "wire format (sc8, sc16 or s16)")
        ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
        ("skip-lo", "skip checking LO lock status")
        ("int-n", "tune USRP with integer-N tuning")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << boost::format("UHD RX samples to file %s") % desc << std::endl;
        std::cout << std::endl
                  << "This application streams data from a single channel of a USRP "
                     "device to a file.\n"
                  << std::endl;
        return ~0;
    }

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // Lock mboard clocks
    if (vm.count("ref")) {
        usrp->set_clock_source(ref);
    }

    // always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("subdev"))
        usrp->set_rx_subdev_spec(subdev);

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    // set the sample rate
    if (rate <= 0.0) {
        std::cerr << "Please specify a valid sample rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_rx_rate(rate, channel);
    std::cout << boost::format("Actual RX Rate: %f Msps...")
                     % (usrp->get_rx_rate(channel) / 1e6)
              << std::endl
              << std::endl;

    // set the center frequency
    if (vm.count("freq")) { // with default of 0.0 this will always be true
        std::cout << boost::format("Setting RX Freq: %f MHz...") % (freq / 1e6)
                  << std::endl;
        std::cout << boost::format("Setting RX LO Offset: %f MHz...") % (lo_offset / 1e6)
                  << std::endl;
        uhd::tune_request_t tune_request(freq, lo_offset);
        if (vm.count("int-n"))
            tune_request.args = uhd::device_addr_t("mode_n=integer");
        usrp->set_rx_freq(tune_request, channel);
        std::cout << boost::format("Actual RX Freq: %f MHz...")
                         % (usrp->get_rx_freq(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the rf gain
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
        usrp->set_rx_gain(gain, channel);
        std::cout << boost::format("Actual RX Gain: %f dB...")
                         % usrp->get_rx_gain(channel)
                  << std::endl
                  << std::endl;
    }

    // set the IF filter bandwidth
    if (vm.count("bw")) {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        usrp->set_rx_bandwidth(bw, channel);
        std::cout << boost::format("Actual RX Bandwidth: %f MHz...")
                         % (usrp->get_rx_bandwidth(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the antenna
    if (vm.count("ant"))
        usrp->set_rx_antenna(ant, channel);

    std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(1000 * setup_time)));

    // check Ref and LO Lock detect
    if (not vm.count("skip-lo")) {
        check_locked_sensor(usrp->get_rx_sensor_names(channel),
            "lo_locked",
            [usrp, channel](const std::string& sensor_name) {
                return usrp->get_rx_sensor(sensor_name, channel);
            },
            setup_time);
        if (ref == "mimo") {
            check_locked_sensor(usrp->get_mboard_sensor_names(0),
                "mimo_locked",
                [usrp](const std::string& sensor_name) {
                    return usrp->get_mboard_sensor(sensor_name);
                },
                setup_time);
        }
        if (ref == "external") {
            check_locked_sensor(usrp->get_mboard_sensor_names(0),
                "ref_locked",
                [usrp](const std::string& sensor_name) {
                    return usrp->get_mboard_sensor(sensor_name);
                },
                setup_time);
        }
    }

    
    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    

    recv_and_proc(usrp,                        \
        wirefmt,                  \
        channel,                  \
        nch, 
        batch
        );



    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
