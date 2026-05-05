CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread

TARGET = myshell
OBJS = main.o parser.o executor.o pipeline.o builtins.o

CLIENT = myshell_client
SERVER = myshell_server
DEMO = demo

all: $(TARGET) $(CLIENT) $(SERVER) $(DEMO)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(CLIENT): myshell_client.c myshell_net.h
	$(CC) $(CFLAGS) -o $(CLIENT) myshell_client.c

$(SERVER): myshell_server.c myshell_net.h parser.o executor.o pipeline.o builtins.o
	$(CC) $(CFLAGS) -o $(SERVER) myshell_server.c parser.o executor.o pipeline.o builtins.o

$(DEMO): demo.c
	$(CC) $(CFLAGS) -o $(DEMO) demo.c

main.o: main.c executor.h pipeline.h
parser.o: parser.c parser.h
executor.o: executor.c executor.h parser.h
pipeline.o: pipeline.c pipeline.h parser.h executor.h
builtins.o: builtins.c builtins.h

clean:
	rm -f $(TARGET) $(OBJS) $(CLIENT) $(SERVER) $(DEMO)
