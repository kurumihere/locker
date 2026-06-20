CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -pthread `pkg-config --cflags xft fontconfig`
LDFLAGS = -pthread -lX11 -lXss -lpam -lXext `pkg-config --libs xft fontconfig`
SRC = main.c x11.c pam_auth.c util.c
OBJ = $(SRC:.c=.o)

locker: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f locker *.o

.PHONY: clean
