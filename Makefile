CC = gcc
CFLAGS = -Wall -Wextra -Werror -g
LDFLAGS = -static
PROGS = server client

all: $(PROGS)

server: server.c timestamp.c twamp.h

client: client.c timestamp.c twamp.h

setcap:
	sudo setcap 'cap_net_bind_service=+ep' ./server

indent:
	indent -linux -i4 -ts4 -nut *.c *.h

clean:
	rm -f $(PROGS)
