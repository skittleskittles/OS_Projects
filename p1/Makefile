#
# Tony Givargis
# Copyright (C), 2023
# University of California, Irvine
#
# CS 238P - Operating Systems
# Makefile
#

CC     = gcc
CFLAGS = -ansi -pedantic -Wall -Wextra -Werror -Wfatal-errors -fpic -O3
LDLIBS =
DEST   = cs238
SRCS  := $(wildcard *.c)
OBJS  := $(SRCS:.c=.o)
DEBUG  = -g
MATH   = -lm # for exp(double)

all: $(OBJS)
	@echo "[LN]" $(DEST)
	@$(CC) $(DEBUG) -o $(DEST) $(OBJS) $(LDLIBS) $(MATH)

%.o: %.c
	@echo "[CC]" $<
	@$(CC) $(CFLAGS) -c $<
	@$(CC) $(CFLAGS) -MM $< > $*.d

clean:
	@rm -f $(DEST) *.so *.o *.d *~ *#

-include $(OBJS:.o=.d)
