prefix = /usr/local
bindir = $(prefix)/bin
cfgdir = /etc
srvdir = /etc/systemd/system

PROG = cfddns
SRCS = cfddns.c
OBJS = $(SRCS:.c=.o)

LIBS = -lcurl

CFLAGS += -std=c11 -O2 -Wall

all: $(PROG)

install: $(PROG)
	install -D -m 755 $(PROG) $(bindir)/$(PROG)

uninstall:
	-rm -f $(bindir)/$(PROG)

install-systemd: $(PROG)
	install -D -m 755 $(PROG) $(bindir)/$(PROG)
	install -D -m 644 $(PROG).service $(srvdir)/$(PROG).service
	install -D -m 644 $(PROG).timer $(srvdir)/$(PROG).timer
	install -D -m 600 $(PROG).conf $(cfgdir)/$(PROG).conf
	systemctl daemon-reload
	systemctl enable $(PROG).timer

uninstall-systemd: $(PROG)
	-systemctl stop $(PROG).timer
	-systemctl disable $(PROG).timer
	-rm -f $(srvdir)/$(PROG).timer
	-rm -f $(srvdir)/$(PROG).service
	-rm -f $(bindir)/$(PROG)

clean:
	-rm -f $(PROG)
	-rm -f $(OBJS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INC) $(PIC) -o $@ $<

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

.PHONY: all install uninstall install-systemd uninstall-systemd clean
