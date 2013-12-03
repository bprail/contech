PROJECT = libCommTracker.a
CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= CommTracker.o CommRecord.o
INCLUDES=
LIBS= -lz -L../../common/taskLib/ -lTask
EXTERNALS=

all: taskLib $(PROJECT)

taskLib:
	$(MAKE) -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@ 
$(PROJECT): $(OBJECTS)
	ar rc $(PROJECT) $(OBJECTS) 

clean:
	rm -f *.o
	rm -f $(PROJECT)
