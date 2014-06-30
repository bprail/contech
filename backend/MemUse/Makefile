PROJECT = memUse
CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECTS= main.o
LPROJ = libmemUse.a
LOBJ = memUse.o
INCLUDES=
LIBS= -L../../common/taskLib/ -lTask -lz -L.
EXTERNALS=

all: taskLib $(LPROJ) $(PROJECT)

taskLib:
	$(MAKE) -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $< $(CXXFLAGS) $(INCLUDES) -c -o $@ 
$(PROJECT): $(OBJECTS) $(LOBJ)
	$(CXX) -o $(PROJECT) $(OBJECTS) $(CXXFLAGS) $(LIBS) -l$(PROJECT)

$(LPROJ): $(LOBJ)
	ar rc $(LPROJ) $(LOBJ) 

clean:
	rm -f *.o
	rm -f $(PROJECT)
