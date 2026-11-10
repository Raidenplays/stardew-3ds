# PC screenshot tool (not part of the 3DS build)
ROOT := ../..
CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -I$(ROOT)/source -I. -DREPO_ROMFS=\"$(abspath $(ROOT))/romfs\"
SRC := preview.cpp host_canvas.cpp $(wildcard $(ROOT)/source/core/*.cpp) $(ROOT)/source/render/scene.cpp $(ROOT)/source/render/modscreen.cpp $(ROOT)/source/render/fonts.cpp $(ROOT)/source/render/title.cpp

preview: $(SRC) $(wildcard *.h) $(wildcard $(ROOT)/source/core/*.h) $(wildcard $(ROOT)/source/render/*.h)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -f preview
