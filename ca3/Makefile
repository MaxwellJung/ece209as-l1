CXXFLAGS := -g -Wall --std=c++11
CXX=g++
SRC=example/maxwell.cc lib/config1.a
bin=maxwell-config1

run: build
	./$(bin) -warmup_instructions 1000000 -simulation_instructions 10000000 -traces trace/bzip2_10M.trace.gz

build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(bin)

clean:
	rm -f $(bin)
