# Real-time waterfall diagram from USRP data acquisition
## Purpose
Performing USRP data acquisition and display waterfall diagram. New function may be added later.

## Design
This application is composed of two programs: `rt_waterfall` and `monitor`.

### `rt_waterfall`
This program is for performing data acquisition and FFT. The result is stored in a shared memory, which is to be read from other programs (currently `monitor`).

### `monitor`
It is the only visualization program by now. It reads the production of `rt_waterfall` and show the waterfall diagram on the screen.

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

4. sysv_ipc (for `monitor.py`)
```
sudo apt install python3-sysv-ipc
```

## Building
```
make clean&&make
```

## Example

### First run the data acquisiton program
In one console run
```
./build/rt_waterfall --rate 61.44e6 --freq 100e6 --nch 2048 --batch 32 --wirefmt sc8 --gain 10 --args 'name=MyB210' --bw 40e6 --nbatch 64
```

The `--args 'name=MyB210'` is optional when there is only one USRP device connected to the host.
It is written based on the UHD host example `rx_sample_to_file.cpp`

### Then run the monitoring program
In this step you have two options

One option is to invoke a C++ version monitor: In another console run
```
./build/monitor --mindb -10 --maxdb 100
```

The other one option is to invoke a more pretty (but relatively slower) pythoner version monitor: In another console run
```
./py/monitor.py
```
