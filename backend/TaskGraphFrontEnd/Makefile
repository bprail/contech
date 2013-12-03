PROJECT = libTaskGraphFrontEnd.a
CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= TaskGraphFrontEnd.o markedInstruction.o markedBlock.o markedCodeContainer.o
INCLUDES=
LIBS= -lz -L../../common/taskLib/ -lTask
EXTERNALS=

all: $(PROJECT)

install: all
	cp IFrontEnd.hpp /net/tinker/local/include/
	cp libTaskGraphFrontEnd.a /net/tinker/local/lib/

markedCodeContainerTest: all markedCodeContainer_test.o
	$(CXX) $(CXXFLAGS) markedCodeContainer_test.o markedInstruction.o markedBlock.o markedCodeContainer.o -o markedCodeContainer_test 
	./markedCodeContainer_test

TaskGraphFrontEndTest: all markedCodeContainer_test.o TaskGraphFrontEnd_test.o
	# $(CXX) TaskGraphFrontEnd_test.o $(CXXFLAGS) $(LIBS) -L. -lTaskGraphFrontEnd -o TaskGraphFrontEnd_test
	 g++ TaskGraphFrontEnd_test.o ../../common/taskLib/Task.o ../../common/taskLib/ct_file.o -g -std=c++11 -O3 -L../../common/taskLib/ -lTask -lz -L. -lTaskGraphFrontEnd -o TaskGraphFrontEnd_test
	./TaskGraphFrontEnd_test
	
test: markedCodeContainerTest TaskGraphFrontEndTest

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@ 
$(PROJECT): $(OBJECTS)
	ar rc $(PROJECT) $(OBJECTS) 

clean:
	rm -f *.o
	rm -f $(PROJECT)
