# PC screenshot tool (not part of the 3DS build)
ROOT := ../..
CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -I$(ROOT)/source -I.
SRC := preview.cpp host_canvas.cpp $(wildcard $(ROOT)/source/core/*.cpp) $(ROOT)/source/render/scene.cpp

preview: $(SRC) $(wildcard *.h) $(wildcard $(ROOT)/source/core/*.h) $(wildcard $(ROOT)/source/render/*.h)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -f preview
