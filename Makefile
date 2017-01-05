CFLAGS = -g -Wall -std=c99 -lm

all: life-client.o life-server.o life-worker.o
	gcc life-client.o -o life-client -g -lm
	gcc life-server.o -o life-server -g -lm
	gcc life-worker.o -o life-worker -g -lm

life-client.o: life-client.c
	gcc $(CFLAGS) -c life-client.c -o life-client.o
life-server.o: life-server.c
	gcc $(CFLAGS) -c life-server.c -o life-server.o
life-worker.o: life-worker.c
	gcc $(CFLAGS) -c life-worker.c -o life-worker.o

docs:
	doxygen Doxyfile

clean:
	rm -rf *.o life-client life-server life-worker
