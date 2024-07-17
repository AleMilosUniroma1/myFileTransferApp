SERVER_ARGS= -a 0.0.0.0 -p 1024 -d root
# CFLAGS = -std=c99 -ggdb -Wall -Wextra -pedantic -pedantic-errors

CFLAGS += -ggdb

all: run

run: myFTserver 
	 ./myFTserver ${SERVER_ARGS}

myFTserver: myFTserver.o utils.o
	$(CC) -pthread $(LDFLAGS) -o $@ $^

myFTclient: myFTclient.o utils.o
	$(CC) $(LDFLAGS) -o $@ $^

myFTserver.o: myFTserver.c
	$(CC) $(CFLAGS) -c -o $@ $<

myFTclient.o: myFTclient.c
	$(CC) $(CFLAGS) -c -o $@ $<

utils.o: utils.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o myFTclient myFTserver 

.PHONY: all run clean
