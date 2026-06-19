CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic
LDFLAGS = -lX11 -lpam
SRC = main.c x11.c pam_auth.c
OBJ = $(SRC:.c=.o)

locker: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f locker *.o

.PHONY: clean
