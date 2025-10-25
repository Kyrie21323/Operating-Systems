# Compiler and flags
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
# Define POSIX for strtok_r and other POSIX functions
CFLAGS += -D_POSIX_C_SOURCE=200809L

INCDIR  := include
SRCDIR  := src
OBJDIR  := build

TARGET  := myshell

INCLUDES := -I$(INCDIR)

SRC := \
  $(SRCDIR)/main.c \
  $(SRCDIR)/parse.c \
  $(SRCDIR)/exec.c  \
  $(SRCDIR)/redir.c \
  $(SRCDIR)/tokenize.c \
  $(SRCDIR)/util.c

OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)