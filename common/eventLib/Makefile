PROJECT = libct_event.a
OBJECTS = ct_event.o
CFLAGS  = -O2 -g --std=c++11 
HEADERS = ct_event.h ct_event_st.h

all: $(PROJECT)

.cpp.o:
	g++ -c $(CFLAGS) $<

$(PROJECT): $(OBJECTS)
	ar rc $(PROJECT) $(OBJECTS)

clean:
	rm $(PROJECT) $(OBJECTS)
