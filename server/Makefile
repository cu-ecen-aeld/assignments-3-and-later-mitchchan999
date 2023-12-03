CC=$(CROSS_COMPILE)gcc
CFLAGS=

default: aesdsocket

aesdsocket.o: aesdsocket.c 
	$(CC) -c -o $@ $< $(CFLAGS)

aesdsocket: aesdsocket.o
	$(CC) -o $@ $^ $(CFLAGS) 

.PHONY: clean

clean:
	rm -f *.o aesdsocket 