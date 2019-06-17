prefix = /usr/local
bindir = $(prefix)/bin

PROG = cfddns
SRCS = cfddns.c
OBJS = $(SRCS:.c=.o)

LIBS = -lcurl

CFLAGS += -std=c11 -O2 -Wall

all: $(PROG)

install: $(PROG)
	install -d $(bindir)
	install -D -m 755 $(PROG) $(bindir)/$(PROG)

remove:
	-rm -f $(bindir)/$(PROG)

clean:
	-rm -f $(PROG)
	-rm -f $(OBJS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INC) $(PIC) -o $@ $<

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

.PHONY: all clean install