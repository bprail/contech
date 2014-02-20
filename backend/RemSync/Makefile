CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= remSync.o
INCLUDES=
LIBS= -L../../common/taskLib -lTask -lz

all: taskLib remSync

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
remSync: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o
	rm -f remSync
