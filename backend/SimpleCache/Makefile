PROJECT = scache
CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= main.o simpleCache.o
INCLUDES= -I../../common/taskLib/
LIBS= -L../../common/taskLib/ -lTask -lz -L. -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/
EXTERNALS=

all: taskLib $(PROJECT)

taskLib:
	$(MAKE) -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@
$(PROJECT): $(OBJECTS)
	$(CXX) -o $(PROJECT) $(OBJECTS) $(CXXFLAGS) $(LIBS)

clean:
	rm $(OBJECTS) $(PROJECT)

