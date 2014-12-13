PROJECT = libct_event.a
OBJECTS = ct_event.o
CFLAGS  = -O2 -g 
HEADERS = ct_event.h ct_event_st.h

all: $(PROJECT)

.cpp.o:
	g++ -c $(CFLAGS) $<

$(PROJECT): $(OBJECTS)
	ar rc $(PROJECT) $(OBJECTS)

install: all
	mkdir -p $(CONTECH_LLVM_HOME)/lib
	mkdir -p $(CONTECH_LLVM_HOME)/include
	cp $(PROJECT) $(CONTECH_LLVM_HOME)/lib
	cp $(HEADERS) $(CONTECH_LLVM_HOME)/include

clean:
	rm $(PROJECT) $(OBJECTS)
