#include <sys/ipc.h>
#include <sys/shm.h>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <atomic>
#include <SDL2/SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>
#include <config.hpp>
#include <shm_info.hpp>
namespace po = boost::program_options;

std::uint32_t rainbow(SAMP_TYPE x, SAMP_TYPE xmin, SAMP_TYPE xmax){
    auto f=std::sqrt((x-xmin)/(xmax-xmin));
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



int main(int argc, char* argv[]){
    SAMP_TYPE min_db, max_db;


    int shmid_daq_info = shmget(daq_info_key,sizeof(DaqInfo),0666|IPC_CREAT);
    assert(shmid_daq_info!=-1);
    DaqInfo* pdaq_info=(DaqInfo*) shmat(shmid_daq_info,(void*)0,0);
    assert(pdaq_info->init_magic==INIT_MAGIC);
    size_t nch=pdaq_info->nch;
    size_t batch=pdaq_info->batch;
    size_t nbatch=pdaq_info->nbatch;
    std::cerr<<nch<<std::endl;
    std::cerr<<batch<<std::endl;
    std::cerr<<nbatch<<std::endl;
    int shmid_payload = shmget(payload_key, sizeof(SAMP_TYPE)*nch*batch*nbatch, 0666|IPC_CREAT);
    assert(shmid_payload!=-1);
    SAMP_TYPE* waterfall_payload_buf=(SAMP_TYPE*) shmat(shmid_payload, (void*)0, 0);

    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("mindb", po::value<SAMP_TYPE>(&min_db)->default_value(-30.0), "min waterfall value in dB")
        ("maxdb", po::value<SAMP_TYPE>(&max_db)->default_value(30.0), "max waterfall value in dB")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << boost::format("UHD RX samples to file %s") % desc << std::endl;
        std::cout << std::endl
                  << "This application streams data from a single channel of a USRP "
                     "device to a file.\n"
                  << std::endl;
        return ~0;
    }


    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
            return ~0;
    }
    SDL_Window *window=SDL_CreateWindow("waterfall",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            800,600,
                            SDL_WINDOW_RESIZABLE);

    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create window: %s\n", SDL_GetError());
        return ~0;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create renderer: %s\n", SDL_GetError());
        return ~0;
    }

    SDL_Texture *waterfallTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, nch, batch*nbatch);

    if (!waterfallTexture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create texture: %s\n", SDL_GetError());
        return ~0;
    }

    std::atomic<bool> stop=false;
    for(int i=0;!stop;++i)
    {
        std::cerr<<i<<std::endl;
        //std::ofstream ofs("a.bin");
        //ofs.write((char*)data->payload.data(), sizeof(std::complex<SAMP_TYPE>)*data->payload.size());
        //ofs.close();
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    stop=true;
                }
                break;
            case SDL_QUIT:
                stop=true;
                break;
            }
        }


        size_t idx=0;
        void *pixels;
        int pitch;
        if (SDL_LockTexture(waterfallTexture, NULL, &pixels, &pitch) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't lock texture: %s\n", SDL_GetError());
            return ~0;
        }

        for(int i=0;i<batch*nbatch;++i){
            auto dst = (std::uint32_t*)((std::uint8_t*)pixels + i * pitch);
            for(int j=0;j<nch;++j){
                std::uint32_t color=rainbow(std::log10(waterfall_payload_buf[idx++])*10, min_db, max_db);
                *dst++=color;
            }
        }
        SDL_UnlockTexture(waterfallTexture);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, waterfallTexture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    

   shmdt(pdaq_info);
   shmdt(waterfall_payload_buf);
}