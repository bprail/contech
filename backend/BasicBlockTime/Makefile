CXX = g++
PROJECT = bbTime
OBJECTS = main.o bbDelta.o
CPPFLAGS  = -O3 -g --std=c++11 -I../../common/taskLib/
LIBS = -L../../common/taskLib/ -lTask -lz -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/

all: $(PROJECT) traverse

$(PROJECT): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LIBS) -o $(PROJECT) 

traverse: traverse.o
	$(CXX) traverse.o $(LIBS) -o traverse

clean:
	rm -f $(PROJECT) $(OBJECTS)

	
