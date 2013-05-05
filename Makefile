CC = gcc
CCFLAGS = -Wall -lm -g
SRC1 = server.c
PROGRAM1 = server
SRC2 = client.c
PROGRAM2 = client
SRCS = timestamp.c
build: server client

server:
	$(CC) -o $(PROGRAM1) $(SRC1) $(SRCS) $(CCFLAGS)

client:
	$(CC) -o $(PROGRAM2) $(SRC2) $(SRCS) $(CCFLAGS)	


indent:
	indent -linux -i4 -ts4 *.c *.h

.PHONY: clean server client build
clean:
	rm -f $(PROGRAM1) $(PROGRAM2) *~