CXX=g++
CXXFLAGS= -g -std=c++11 -O3
OBJECT_PROF= easyProf.o
OBJECT_VIZ= easyViz.o
INCLUDES=
LIBS= -L/net/tinker/local/lib64 -L/net/tinker/local/lib -lgvc -lcgraph -L../../common/taskLib -lTask -lz -Wl,-rpath=$(CONTECH_HOME)/common/taskLib/

all: taskLib easyProf easyViz

taskLib:
	make -C ../../common/taskLib

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

easyProf: $(OBJECT_PROF)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

easyViz: $(OBJECT_VIZ)
	$(CXX) $^ $(CXXFLAGS) $(LIBS) -o $@  $(EXTERNALS)

clean:
	rm -f *.o
	rm -f easyProf easyViz
