PREFIX ?= 
BINDIR ?= $(PREFIX)/sbin
ETCDIR ?= $(PREFIX)/etc
CONFDIR ?= $(ETCDIR)/jalauditd
JALCONF ?= jalauditd.conf
AUCONFDIR ?= $(ETCDIR)/audit/plugins.d
AUCONF ?= audisp-jalauditd.conf

JALAUDITD = jalauditd

SRC = $(JALAUDITD).c
OBJ = $(JALAUDITD).o

ARCH := $(shell getconf LONG_BIT)

LDFLAGS_32 ?= -lauparse -lconfig -ljal-producer -ljal-common -L/usr/local/lib -L/lib -lglib-2.0 -lrt -lpthread -pie -Wl,-z,relro,-z,now
CFLAGS_32 ?= -Werror -Wall -Wshadow -Wextra -Wundef -Wmissing-format-attribute -Wcast-align -Wstrict-prototypes -Wpointer-arith -Wunused -D_GNU_SOURCE -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -O2 -fstack-protector-all -Wstack-protector -D_FORTIFY_SOURCE=2 -fPIE

LDFLAGS_64 ?= -lauparse -lconfig -ljal-producer -ljal-common -lglib-2.0 -lrt -lpthread -pie -Wl,-z,relro,-z,now
CFLAGS_64 ?= -Werror -Wall -Wshadow -Wextra -Wundef -Wmissing-format-attribute -Wcast-align -Wstrict-prototypes -Wpointer-arith -Wunused -D_GNU_SOURCE -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -O2 -fstack-protector-all -Wstack-protector -D_FORTIFY_SOURCE=2 -fPIE

LDFLAGS := $(LDFLAGS_$(ARCH))
CFLAGS := $(CFLAGS_$(ARCH))

ifeq ($(DEBUG),1)
	override CFLAGS += -g3 -O0 -gdwarf-2 -fno-strict-aliasing -DDEBUG
	override LDFLAGS += -g
else
	override LDFLAGS += -s
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
