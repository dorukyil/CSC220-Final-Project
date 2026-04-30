CC       = gcc
CFLAGS   = -Wall -Wextra -pthread -std=gnu99
TARGET   = motorcycle_os
SRCS     = main.c engine.c motion.c fuel.c ecu.c dashboard.c init.c
HDRS     = system_state.h subsystems.h
OBJS     = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
