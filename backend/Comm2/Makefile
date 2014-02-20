CC=g++
CXX=g++
CFLAGS= -g -O3
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= comm.o commFileIO.o
INCLUDES=
LIBS= -L../../common/taskLib/ -lTask -L../CommTracker -lCommTracker -lz
EXTERNALS=

all: taskLib comm 

taskLib:
	make -C ../../common/taskLib
	
%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@ 
comm: $(OBJECTS)
	$(CXX) $^ $(CXXFLAGS) $(LIBS) $(EXTERNALS) -o $@ 

clean:
	rm -f *.o
	rm -f comm
