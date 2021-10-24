# real-time waterfall diagram from USRP data acquisition

## Dependencies
1. SDL2
```
sudo apt install libsdl2-dev
```

2. UHD
```
sudo apt install libuhd-dev uhd-host
```

3. FFTW
```
sudo apt install libfftw3-dev
```

## Building
```
make clean&&make
```

## Example
In one console run
```
cd build
./rt_waterfall --rate 61.44e6 --freq 100e6 --nch 2048 --batch 32 --wirefmt sc8 --gain 10 --args 'name=MyB210' --bw 40e6 --nbatch 64
```

The `--args 'name=MyB210'` is option when there is only one USRP device connected to the host.
It is written based on the UHD host example `rx_sample_to_file.cpp`

In another console run
```
cd build
./monitor --mindb -10 --maxdb 100
```
