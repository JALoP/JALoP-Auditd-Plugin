PREFIX ?= 
BINDIR ?= $(PREFIX)/sbin
ETCDIR ?= $(PREFIX)/etc
CONFDIR ?= $(ETCDIR)/jalauditd
JALCONF ?= jalauditd.conf
AUCONFDIR ?= $(ETCDIR)/audisp/plugins.d
AUCONF ?= audisp-jalauditd.conf

JALAUDITD = jalauditd

SRC = $(JALAUDITD).c
OBJ = $(JALAUDITD).o

LDFLAGS ?= -lauparse -lconfig -ljal-producer -ljal-common -L/usr/local/lib -Wl,-rpath=/usr/local/lib
CFLAGS ?= -Werror -Wall -Wshadow -Wextra -Wundef -Wmissing-format-attribute -Wcast-align -Wstrict-prototypes -Wpointer-arith -Wunused -D_GNU_SOURCE

ifeq ($(DEBUG),1)
	override CFLAGS += -g3 -O0 -gdwarf-2 -fno-strict-aliasing -DDEBUG
	override LDFLAGS += -g
else
	override CFLAGS += -O2
endif

all: $(JALAUDITD)

$(JALAUDITD): $(SRC)
	$(CC) -c $^ $(CFLAGS)
	$(CC) $(OBJ) -o $(JALAUDITD) $(LDFLAGS)

install: $(JALAUDITD)
	install -D -m 0750 $(JALAUDITD) $(BINDIR)/$(JALAUDITD)
	install -D -m 0644 $(JALCONF) $(CONFDIR)/$(JALCONF)
	install -D -m 0644 $(AUCONF) $(AUCONFDIR)/$(AUCONF)

uninstall:
	rm -rf $(BINDIR)/$(JALAUDITD)
	rm -rf $(CONFDIR)
	rm -rf $(AUCONFDIR)/$(AUCONF)

clean:
	rm -rf $(JALAUDITD)
	rm -rf $(JALAUDITD).o

.PHONY: all clean install uninstall
