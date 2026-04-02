CC = gcc
CFLAGS = -Wall -Wextra -std=c99

TARGET = myshell
OBJS = main.o parser.o executor.o pipeline.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c executor.h pipeline.h
parser.o: parser.c parser.h
executor.o: executor.c executor.h parser.h
pipeline.o: pipeline.c pipeline.h parser.h executor.h

clean:
	rm -f $(TARGET) $(OBJS)
