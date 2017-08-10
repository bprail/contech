CXX=g++
CXXFLAGS= -g -O3 -std=c++11
OBJECTS= dynamicCFG.o BasicBlock.o
INCLUDES= -I. 
LIBS= -L/net/tinker/local/lib64 -L/net/tinker/local/lib -L../../common/taskLib/ -lgvc -lcgraph -lTask -L../CommTracker/ -lCommTracker -lz -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/
EXTERNALS=

all: taskLib dynamicCFG

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@
dynamicCFG : $(OBJECTS)
	$(CXX) $^ $(CXXFLAGS) $(LIBS) $(EXTERNALS) -o $@ 

clean:
	rm -f *.o
	rm -f dynamicCFG
