CXX = g++
PROJECT = traceValidator
OBJECTS = TraceValidator.o main.o
CPPFLAGS  = -O3 -g --std=c++11
LIBS = -lz -lct_event -L../../common/eventLib/ -lTask -L../../common/taskLib/

all: $(PROJECT)

$(PROJECT): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LIBS) -o $(PROJECT) 

clean:
	rm -f $(PROJECT) $(OBJECTS)

	
