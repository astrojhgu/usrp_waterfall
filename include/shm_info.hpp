#ifndef SHM_INFO_HPP
#define SHM_INFO_HPP
constexpr uint32_t INIT_MAGIC=0x12345;

struct DaqInfo{
    size_t nch;
    size_t batch;
    size_t nbatch;
    uint32_t init_magic;
};


#endif
