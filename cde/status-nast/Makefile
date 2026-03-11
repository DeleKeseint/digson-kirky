CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lncurses

PREFIX = /data/data/com.termux/files/usr
BINDIR = $(PREFIX)/bin
ETCDIR = $(PREFIX)/etc/digson/cde/nastconfig

all: status-nast startupstatusnast

status-nast: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDFLAGS)

startupstatusnast: startup.c
	$(CC) $(CFLAGS) -o $@ startup.c

install: all
	mkdir -p $(ETCDIR)
	cp status-nast startupstatusnast $(BINDIR)/
	chmod 755 $(BINDIR)/status-nast $(BINDIR)/startupstatusnast
	echo "DISABLE_ALL_OPTIONS=0" > $(ETCDIR)/Configfile
	echo "CHECK_GUI_DEFAULT=1" >> $(ETCDIR)/Configfile
	echo "CTRL_X_LIST_GUI=1" >> $(ETCDIR)/Configfile
	echo "USE_TILED=0" >> $(ETCDIR)/Configfile

clean:
	rm -f status-nast startupstatusnast

.PHONY: all install clean
