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
```
./rt_waterfall --rate 61.44e6 --freq 100e6 --nch 4096 --batch 1024 --wirefmt sc8 --gain 20 --args 'name=MyB210'
```

The `--args 'name=MyB210'` is option when there is only one USRP device connected to the host.
It is written based on the UHD host example `rx_sample_to_file.cpp`
