CXX = g++
PROJECT = middle
OBJECTS = middle.o Context.o BarrierWrapper.o
CPPFLAGS  = -O3 -g --std=c++11 -pthread
LIBS = -lTask -lct_event -lz

all: $(PROJECT)

$(PROJECT): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LIBS) -L../common/eventLib/ -L../common/taskLib/ -pthread -o $(PROJECT) 

clean:
	rm -f $(PROJECT) $(OBJECTS) 

	
