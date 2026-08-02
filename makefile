CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -g
LDLIBS = -pthread

TARGET = linked_list_threads
SOURCE = main.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean