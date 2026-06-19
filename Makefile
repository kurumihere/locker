CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic
SRC = main.c x11.c pam_auth.c util.c

HAVE_WL := $(shell pkg-config --exists wayland-client 2>/dev/null && echo yes)
ifdef HAVE_WL
CFLAGS  += $(shell pkg-config --cflags wayland-client xkbcommon)
LDFLAGS += $(shell pkg-config --libs wayland-client xkbcommon)
SRC     += ext-session-lock-v1.c wayland.c
endif

LDFLAGS += -lX11 -lpam
OBJ = $(SRC:.c=.o)

locker: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f locker *.o

wayland.c: ext-session-lock-v1.h

ext-session-lock-v1.h ext-session-lock-v1.c: /usr/share/wayland-protocols/staging/ext-session-lock/ext-session-lock-v1.xml
	wayland-scanner client-header $< ext-session-lock-v1.h
	wayland-scanner private-code $< ext-session-lock-v1.c

.PHONY: clean
