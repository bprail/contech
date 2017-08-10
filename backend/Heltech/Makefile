CXX=g++
CXXFLAGS= -std=c++11 -O3 -g
OBJECTS= heltech.o
INCLUDES= -I.
LIBS= -L/net/tinker/local/lib64 -L/net/tinker/local/lib -L../../common/taskLib/ -lTask -lz -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/
EXTERNALS=

all: taskLib heltech

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@ 
heltech: $(OBJECTS)
	$(CXX) $^ $(CXXFLAGS) $(LIBS) -o $@  $(EXTERNALS)

clean:
	rm -f *.o
	rm -f heltech
