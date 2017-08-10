CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= main.o
INCLUDES=
LIBS= -L../../common/taskLib -lTask -lz -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/

all: taskLib critPath

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
critPath: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o
	rm -f critPath
