CFLAGS=-fPIC -Wall -Werror -I.
OBJ=noly.o

.PHONY:shared
all: shared static

shared: $(OBJ)
	$(CC) -shared -o libnoly.so $(OBJ) $(CFLAGS)
static: $(OBJ)
	$(AR) rcs libnoly.a $(OBJ)
clean:
	rm -rf *.o *.so
