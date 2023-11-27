CC=$(CROSS_COMPILE)gcc
CFLAGS=

default: writer

writer.o: writer.c 
	$(CC) -c -o $@ $< $(CFLAGS)

writer: writer.o
	$(CC) -o $@ $^ $(CFLAGS) 

.PHONY: clean

clean:
	rm -f *.o writer 