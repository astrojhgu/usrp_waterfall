TARGETS=build/rt_waterfall build/monitor
all: $(TARGETS)

LIBS=-luhd -lboost_program_options -pthread -ldl -lfftw3f `pkg-config --libs sdl2`
INC=-I ./include `pkg-config --cflags sdl2`


obj/data_proc.o: src/data_proc.cpp include/data_proc.hpp |obj
	g++ -c -o $@ $< -O3 -g $(INC)


obj/daq_queue.o: src/daq_queue.cpp include/daq_queue.hpp |obj
	g++ -c -o $@ $< -O3 -g $(INC)

obj/utils.o: src/utils.cpp include/utils.hpp |obj
	g++ -c -o $@ $< -O3 -g $(INC)
	

obj/rt_waterfall.o: src/rt_waterfall.cpp |obj
	g++ -c -o $@ $< -O3 -g $(INC)


build/rt_waterfall: obj/rt_waterfall.o obj/daq_queue.o obj/utils.o obj/data_proc.o |build
	g++ $^ -o $@ -O3 $(LIBS) -g


obj/monitor.o: src/monitor.cpp |build
	g++ -c -o $@ $< -O3 -g $(INC)

build/monitor: obj/monitor.o |build
	g++ $^ -o $@ -O3 $(LIBS) -g

obj: 
	mkdir -p obj

build:
	mkdir -p build

clean:
	rm -rf build obj
