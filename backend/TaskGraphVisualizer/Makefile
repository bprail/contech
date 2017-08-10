CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= taskViz.o
INCLUDES= -I.
LIBS= -L/net/tinker/local/lib64 -L/net/tinker/local/lib -lgvc -lcgraph -L../../common/taskLib/ -lTask -L../CommTracker -lCommTracker -lz -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/
EXTERNALS=

all: taskLib taskViz

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@ 
taskViz: $(OBJECTS)
	$(CXX) $^ $(CXXFLAGS) $(LIBS) -o $@  $(EXTERNALS)

clean:
	rm -f *.o
	rm -f taskViz
