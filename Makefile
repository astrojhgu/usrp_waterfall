TARGETS=rt_waterfall
all: $(TARGETS)

LIBS=-luhd -lboost_program_options -pthread -ldl -lfftw3f `pkg-config --libs sdl2`

rt_waterfall.o: rt_waterfall.cpp
	g++ -c -o $@ $< -O3 -g `pkg-config --cflags sdl2`


rt_waterfall: rt_waterfall.o
	g++ $< -o $@ -O3 $(LIBS) -g

clean:
	rm -rf lib $(TARGETS) *.o
