//
// Copyright 2010-2011,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

//#define SHOW_GUI
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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <thread>
#include <fftw3.h>

#include <bufq.hpp>
#include <config.hpp>
#include <daq_queue.hpp>
#include <utils.hpp>
#include <data_proc.hpp>
#include <shm_info.hpp>
#include <functional>
namespace po = boost::program_options;

template <typename T>
std::string type_code(){
    assert(0);
    return "";
}

template <>
std::string type_code<double>(){
    return "fc64";
}

template <>
std::string type_code<float>(){
    return "fc32";
}



SAMP_TYPE smooth_step(SAMP_TYPE x, SAMP_TYPE w){
    constexpr SAMP_TYPE PI=3.14159265358979323846;
    return (std::atan(x/w)+PI/2)/PI;
}

std::uint32_t map_color(SAMP_TYPE x, SAMP_TYPE xmin, SAMP_TYPE xmax){
    auto y=x/xmax;
    uint32_t r=smooth_step(y-0.7, 0.2)*255;
    uint32_t g=smooth_step(y-0.3, 0.2)*255;
    uint32_t b=smooth_step(y-0.1, 0.2)*255;
    return (0xFF000000|(r<<16)|(g<<8)|b);
}

std::uint32_t rainbow(SAMP_TYPE x, SAMP_TYPE xmin, SAMP_TYPE xmax){
    auto f=std::sqrt(x/xmax);
    auto a=(1-f)/0.25;
    auto X=(int)std::floor(a);
    auto Y=(int)std::floor(255*(a-X));
    std::uint32_t r, g, b;
    switch (X){
        case 0:r=255;g=Y;b=0;break;
        case 1: r=255-Y;g=255;b=0;break;
        case 2: r=0;g=255;b=Y;break;
        case 3: r=0;g=255-Y;b=255;break;
        case 4: r=0;g=0;b=255;break;
    }
    return (0xFF000000|(r<<16)|(g<<8)|b);
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
    size_t batch, 
    size_t nbatch,
    float fcenter,
    float bw
    )
{
    size_t samps_per_buff=nch*batch;

    BufQ<DataFrame> bufq{std::make_shared<DataFrame>(0, std::vector<std::complex<SAMP_TYPE>>(samps_per_buff)),
    std::make_shared<DataFrame>(0, std::vector<std::complex<SAMP_TYPE>>(samps_per_buff)),
    std::make_shared<DataFrame>(0, std::vector<std::complex<SAMP_TYPE>>(samps_per_buff))
    };
    
    BufQ<std::tuple<float,float, std::vector<SAMP_TYPE>>> display_data_q{
        std::make_shared<std::tuple<float,float, std::vector<SAMP_TYPE>>>(std::make_tuple(0.0f, 0.0f, std::vector<SAMP_TYPE>())),
        std::make_shared<std::tuple<float,float, std::vector<SAMP_TYPE>>>(std::make_tuple(0.0f, 0.0f, std::vector<SAMP_TYPE>())),
        std::make_shared<std::tuple<float,float, std::vector<SAMP_TYPE>>>(std::make_tuple(0.0f, 0.0f, std::vector<SAMP_TYPE>())),
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
#ifdef SHOW_GUI
    std::thread th_display([&]{
        
    }
    );
#endif

    int shmid_daq_info = shmget(daq_info_key,sizeof(DaqInfo),0666|IPC_CREAT);
    int shmid_payload = shmget(payload_key, sizeof(float)*nch*batch*nbatch, 0666|IPC_CREAT);
    assert(shmid_daq_info!=-1);
    assert(shmid_payload!=-1);
    DaqInfo* pdaq_info=(DaqInfo*) shmat(shmid_daq_info,(void*)0,0);
    float* waterfall_payload_buf=(float*) shmat(shmid_payload, (void*)0, 0);
    pdaq_info->nch=nch;
    pdaq_info->batch=batch;
    pdaq_info->nbatch=nbatch;
    pdaq_info->init_magic=INIT_MAGIC;
    pdaq_info->fcenter=fcenter;
    pdaq_info->bw=bw;

    auto handler=[&](const DataFrame& df){
        //std::vector<float> ampl(df.payload.size());
        int cnt=df.count;
        float* base_ptr=waterfall_payload_buf+nch*batch*(cnt%nbatch);
        for(int i=0;i<df.payload.size();++i){
            base_ptr[i]=std::norm(df.payload[i]);
        }
    };
    
    std::thread th_proc([&]{
        waterfall(bufq, nch, batch, stop_signal_called, handler);
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

#ifdef SHOW_GUI
    th_display.join();
#endif

    shmdt(pdaq_info);
    shmdt(waterfall_payload_buf);
    shmctl(shmid_daq_info,IPC_RMID,NULL);
    shmctl(shmid_payload,IPC_RMID,NULL);
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
    size_t display_buf_nbatch;
    double rate, freq, gain, bw, setup_time, lo_offset;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("nch", po::value<size_t>(&nch)->default_value(1024), "num of chs")
        ("batch", po::value<size_t>(&batch)->default_value(32), "num of batches")
        ("nbatch", po::value<size_t>(&display_buf_nbatch)->default_value(1), "number of batches in display buf")
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
        batch,
        display_buf_nbatch,
        freq,
        rate
        );



    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
