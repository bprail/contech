PROJECT = libct_runtime.a
OBJECTS = ct_runtime.o
CFLAGS  = -O2 -g 
HEADERS = ct_runtime.h
BITCODE = ct_runtime.bc

all: $(PROJECT) $(BITCODE)

.c.o:
	gcc -c $(CFLAGS) $<

$(PROJECT): $(OBJECTS)
	ar scru $(PROJECT) $(OBJECTS)

$(BITCODE): $(OBJECTS)
	$(CONTECH_LLVM_HOME)/bin/clang -emit-llvm -c $(CFLAGS) ct_runtime.c -o ct_runtime.bc -I/net/tinker/local/include -std=c89 -DCT_MAIN

install: all
	cp $(PROJECT) /net/tinker/local/lib
	cp $(HEADERS) /net/tinker/local/include

clean:
	rm $(PROJECT) $(OBJECTS) $(BITCODE)
