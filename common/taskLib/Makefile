PROJECT = libTask.a
OBJECTS = Task.o Action.o ct_file.o ct_graph.o
CC = gcc
CFLAGS = -std=c99 -O3 -g -Wall
CXX = g++
CXXFLAGS  = -std=c++11 -O3 -g -Wall
HEADERS = Task.h 

all: $(PROJECT) ct_file_C.o

$(PROJECT): $(OBJECTS)
	ar rc $(PROJECT) $(OBJECTS)

objects: $(OBJECTS)


# ct_file is compiled separately for c and c++ usage
ct_file.o:
	g++ -c $(CXXFLAGS) ct_file.c -o ct_file.o

ct_file_C.o:
	gcc -c $(CFLAGS) ct_file.c -o ct_file_C.o 

.PHONY: test
test: all Task_test Task_C_test		

Task_test: Task_test.o $(PROJECT)
	$(CXX) $(CXXFLAGS) -o Task_test Task_test.cpp -L. -lTask -lz
	./Task_test
	
Task_C_test: Task_test_C.o ct_file_C.o $(PROJECT)
	$(CC) $(CFLAGS) -o Task_test_C Task_test_C.c ct_file_C.o -lz
	./Task_test_C
	#./ct_bfs_test

.PHONY: clean	
clean:
	rm -f $(PROJECT) $(OBJECTS)
	rm -f Task_test
