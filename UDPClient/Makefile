CC = g++
CFLAGS = -g 
OBJS = client.o

all: udpclient

udpclient: $(OBJS)
	$(CC) $(OBJS) -o udpclient

client.o: client.cpp
	$(CC) $(CFLAGS) -c client.cpp

clean: 
	rm -rf *~ *.o udpclient