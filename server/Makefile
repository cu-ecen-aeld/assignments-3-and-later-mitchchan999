CC=$(CROSS_COMPILE)gcc
CFLAGS=

default: aesdsocket

aesdsocket.o: aesdsocket.c 
	$(CC) -c -o $@ $< $(CFLAGS) -lpthread

aesdsocket: aesdsocket.o
	$(CC) -o $@ $^ $(CFLAGS) -lpthread

.PHONY: clean

clean:
	rm -f *.o aesdsocket 
