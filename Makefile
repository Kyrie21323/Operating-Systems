CC = gcc

# compiler options
# -wall shows all warnings
# -g for debugging
CFLAGS = -Wall -g

# name of program
TARGET = myshell

#c source file
SRCS = myshell.c

# default rule, runs when you type 'make'
all: $(TARGET)

#how to build the program
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

#how to clean the folder
clean:
	rm -f $(TARGET)

