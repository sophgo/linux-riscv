CC = $(CROSS_COMPILE)gcc

SRCS = pmbus.c

OBJS = $(SRCS:.c=.o)

CFLAGS = --static

all : pmbus

efuse : $(OBJS)
	$(CC) -o pmbus $(SRCS) $(CFLAGS)
	rm -f $(OBJS)

.PHONY : clean
clean:
	rm -f $(OBJS) pmbus
