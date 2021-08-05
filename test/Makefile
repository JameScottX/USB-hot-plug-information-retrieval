CC = gcc
CFLAGS= -I .
DEPS = usb_monitor_test.h
OBJ = usb_monitor_test.o 

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

usb_test: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	sudo rm -rf ./usb_test *.o


