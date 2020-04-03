all: ws2812svr

INCL=-I/usr/include
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -lpthread
CC=gcc -g $(INCL)

ifneq (1,$(NO_PNG))
  CC += -DUSE_PNG
  LINK += -lpng
endif

ifneq (1,$(NO_JPEG))
  CC += -DUSE_JPEG
  LINK += -ljpeg
endif

ifneq (1,$(NO_WIRINGPI))
  CC += -DUSE_WIRINGPI
  LINK += -lwiringPi
endif

dma.o: dma.c dma.h
	$(CC) -c $< -o $@

mailbox.o: mailbox.c mailbox.h
	$(CC) -c $< -o $@

pwm.o: pwm.c pwm.h ws2811.h
	$(CC) -c $< -o $@

pcm.o: pcm.c pcm.h
	$(CC) -c $< -o $@

rpihw.o: rpihw.c rpihw.h
	$(CC) -c $< -o $@

ifneq (1,$(NO_PNG))
readpng.o: readpng.c readpng.h
	$(CC) -c $< -o $@
endif

ws2811.o: ws2811.c ws2811.h rpihw.h pwm.h pcm.h mailbox.h clk.h gpio.h dma.h rpihw.h readpng.h
	$(CC) -c $< -o $@

main.o: main.c ws2811.h
	$(CC) -c $< -o $@

ifneq (1,$(NO_PNG))
ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o rpihw.o readpng.o
	$(CC) $(LINK) $^ -o $@
else
ws2812svr: main.o dma.o mailbox.o pwm.o pcm.o ws2811.o rpihw.o
	$(CC) $(LINK) $^ -o $@
endif

clean:
	rm *.o

install: ws2812svr
	cp ws2812svr.service  /etc/systemd/system/ws2812svr.service
	cp -n ws2812svr.conf /etc/ws2812svr.conf
	cp ws2812svr /usr/local/bin
	cp hcuartnetserver.service  /etc/systemd/system/hcuartnetserver.service
	cp hcu-artnet-server/main.js /usr/local/etc/hcu-artnet-server.js
	ln hcu-artnet-server/run.sh /usr/local/bin/hcu-artnet-server
	systemctl daemon-reload
	-systemctl stop ws2812svr.service
	systemctl enable ws2812svr.service
	systemctl start ws2812svr.service
	-systemctl stop hcuartnetserver.service
	systemctl enable hcuartnetserver.service
	systemctl start hcuartnetserver.service

uninstall: ws2812svr
	systemctl stop ws2812svr.service
	systemctl disable ws2812svr.service
	systemctl stop hcuartnetserver.service
	systemctl disable hcuartnetserver.service
	rm /usr/local/bin/hcu-artnet-server
	rm /usr/local/etc/hcu-artnet-server.js
	rm  /etc/systemd/system/ws2812svr.service
	rm /etc/ws2812svr.conf
	rm /usr/local/bin/ws2812svr
	systemctl daemon-reload
