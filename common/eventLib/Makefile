PROJECT = libct_event.a
OBJECTS = ct_event.o
CFLAGS  = -O2 -g 
HEADERS = ct_event.h ct_event_st.h

all: $(PROJECT)

.c.o:
	g++ -c $(CFLAGS) $<

$(PROJECT): $(OBJECTS)
	ar rc $(PROJECT) $(OBJECTS)

install: all
	cp $(PROJECT) /net/tinker/local/lib
	cp $(HEADERS) /net/tinker/local/include

clean:
	rm $(PROJECT) $(OBJECTS)
