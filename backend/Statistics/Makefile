CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= stats.o
INCLUDES=
LIBS= -L../../common/taskLib -lTask -lz -Wl,-rpath=../../common/taskLib/

all: taskLib stats

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
stats: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o
	rm -f stats
