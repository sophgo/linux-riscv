CC = $(CROSS_COMPILE)gcc

SRCS = efuse.c
SRCS += devmem.c
SRCS += argvtoint.c

OBJS = $(SRCS:.c=.o)

CFLAGS = --static

all : efuse

efuse : $(OBJS)
	$(CC) -o efuse $(SRCS) $(CFLAGS)
	rm -f $(OBJS)

.PHONY : clean
clean:
	rm -f $(OBJS) efuse
