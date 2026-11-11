# PC screenshot tool (not part of the 3DS build)
ROOT := ../..
CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -I$(ROOT)/source -I. -DREPO_ROMFS=\"$(abspath $(ROOT))/romfs\"
SRC := preview.cpp host_canvas.cpp $(wildcard $(ROOT)/source/core/*.cpp) $(ROOT)/source/render/scene.cpp $(ROOT)/source/render/modscreen.cpp $(ROOT)/source/render/fonts.cpp $(ROOT)/source/render/title.cpp $(ROOT)/source/render/setupscreen.cpp $(ROOT)/source/render/story.cpp $(wildcard $(ROOT)/source/setup/*.cpp)

all: preview

stb_impl.o: $(ROOT)/source/third_party/stb_impl.c
	$(CC) -O2 -c $< -o $@

preview: stb_impl.o $(SRC) $(wildcard *.h) $(wildcard $(ROOT)/source/core/*.h) $(wildcard $(ROOT)/source/render/*.h) $(wildcard $(ROOT)/source/setup/*.h)
	$(CXX) $(CXXFLAGS) $(SRC) stb_impl.o -o $@

clean:
	rm -f preview stb_impl.o
