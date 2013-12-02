CXX = g++
PROJECT = middle
OBJECTS = middle.o Context.o BarrierWrapper.o
CPPFLAGS  = -O3 -g --std=c++11
LIBS = -lz -lTask -lct_event

all: $(PROJECT)

$(PROJECT): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LIBS) -L../common/eventLib/ -L../common/taskLib/ -o $(PROJECT) 

clean:
	rm -f $(PROJECT) $(OBJECTS) 

	
