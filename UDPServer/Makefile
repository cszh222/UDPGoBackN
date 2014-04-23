CC = g++
CFLAGS = -g 
OBJS = server.o

all: udpserver

udpserver: $(OBJS)
	$(CC) $(OBJS) -o udpserver
server.o: server.cpp
	$(CC) $(CFLAGS) -c server.cpp

clean: 
	rm -rf *~ *.o udpserver