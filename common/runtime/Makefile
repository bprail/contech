PROJECT = libct_runtime.a
OBJECTS = ct_runtime.o
CFLAGS  = -O3
HEADERS = ct_runtime.h
BITCODE = ct_runtime.bc ct_main.bc ct_mpi.bc ct_nompi.bc

.SUFFIXES:
.SUFFIXES: .bc .c

all: $(BITCODE)

.c.bc:  $(HEADERS)
	clang -emit-llvm -I. -c $(CFLAGS) -DCT_MAIN $<

clean:
	rm  $(BITCODE)
