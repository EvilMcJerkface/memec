CC=g++
CFLAGS=-std=c++11 -Wall -O2
MEMEC_SRC_ROOT=../../..
LIBS=

OBJS= \
	protocol

EXTERNAL_LIB=

all: $(OBJS) $(EXTERNAL_LIB)

protocol: protocol.cc $(MEMEC_SRC_ROOT)/common/protocol/protocol.o
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

clean:
	rm -f $(OBJS)
