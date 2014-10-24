PROJECT = libct_runtime.a
OBJECTS = ct_runtime.o
CFLAGS  = -O3 -g 
HEADERS = ct_runtime.h
BITCODE = ct_runtime.bc ct_main.bc

all: $(BITCODE) $(OBJECTS)

.c.o:
	gcc -c $(CFLAGS) $<

$(BITCODE): ct_runtime.c ct_main.c
	$(CONTECH_LLVM_HOME)/bin/clang -emit-llvm -c $(CFLAGS) ct_runtime.c -o ct_runtime.bc -I/net/tinker/local/include -std=c89 -DCT_MAIN
	$(CONTECH_LLVM_HOME)/bin/clang -emit-llvm -c $(CFLAGS) ct_main.c -o ct_main.bc -I/net/tinker/local/include -std=c89 -DCT_MAIN


clean:
	rm  $(BITCODE) $(OBJECTS)
