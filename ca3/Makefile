CXXFLAGS := -g -Wall --std=c++11
CXX=g++
SRC=example/maxwell.cc lib/config1.a
bin=maxwell-config1

build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(bin)

benchmark: build
	python3 benchmark.py

clean:
	rm -f $(bin)
