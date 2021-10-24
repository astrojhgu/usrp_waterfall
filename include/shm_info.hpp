#ifndef SHM_INFO_HPP
#define SHM_INFO_HPP
constexpr uint32_t INIT_MAGIC=0x12345;

struct DaqInfo{
    uint32_t nch;
    uint32_t batch;
    uint32_t nbatch;
    uint32_t init_magic;
    float fcenter;
    float bw;
};


#endif
