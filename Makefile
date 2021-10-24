TARGETS=rt_waterfall monitor
all: $(TARGETS)

LIBS=-luhd -lboost_program_options -pthread -ldl -lfftw3f `pkg-config --libs sdl2`

data_proc.o: data_proc.cpp data_proc.hpp
	g++ -c -o $@ $< -O3 -g


daq_queue.o: daq_queue.cpp daq_queue.hpp
	g++ -c -o $@ $< -O3 -g

utils.o: utils.cpp utils.hpp
	g++ -c -o $@ $< -O3 -g
	

rt_waterfall.o: rt_waterfall.cpp
	g++ -c -o $@ $< -O3 -g `pkg-config --cflags sdl2`


rt_waterfall: rt_waterfall.o daq_queue.o utils.o data_proc.o
	g++ $^ -o $@ -O3 $(LIBS) -g


monitor.o: monitor.cpp
	g++ -c -o $@ $< -O3 -g `pkg-config --cflags sdl2`

monitor: monitor.o
	g++ $^ -o $@ -O3 $(LIBS) -g


clean:
	rm -rf lib $(TARGETS) *.o
