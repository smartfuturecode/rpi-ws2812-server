# Rpi-ws2812-server
This is a small program for driving the WS281x (a.k.a. [NeoPixel](https://www.sparkfun.com/products/12999)) LEDs from a webserver, command line, text file, Python,... using the Raspberry Pi. It uses the rpi_ws281x PWM driver code from jgarff ([https://github.com/jgarff/rpi_ws281x](https://github.com/jgarff/rpi_ws281x)). The LEDs can be controlled by sending text commands to a tcp socket. These commands can be generated by a webserver script, android app,... It's also possible to control the leds directly from the command line or by loading text file containing some predefined color patterns.

# Installation
On the raspberry you open a terminal window and type following commands:
* `sudo apt-get update`
* `sudo apt-get install gcc make git libjpeg-dev libpng-dev wiringpi npm`
* `git clone https://github.com/smartfuturecode/rpi-ws2812-server.git`
* `cd rpi-ws2812-server`
* `make`
* `sudo chmod +x ws2812svr`
* `cd hcu-artnet-server`
* `npm install`

Newer versions require libjpeg-dev and libpng-dev for reading PNG and JPEG images.
If you don't want to use JPEG, PNG or WIRINGPI you can disable this using:
* `make NO_JPEG=1 NO_PNG=1 NO_WIRINGPI=1`

On newer Raspbian (Jessie) operating system the audio output is activated by default, you need to disable this:
You can do this by blacklisting the sound module:
`sudo nano /etc/modprobe.d/snd-blacklist.conf`
```
blacklist snd_bcm2835
```

also in `/boot/config.txt` you comment out the `audio=on` parameter:
```
# Enable audio (loads snd_bcm2835)
#dtparam=audio=on
```
# Hardware
![gpio](gpio-numbers-pi2.png)
Default setup: Leds to GPIO 18 = Pin 12 (see `setup` comand)

# Testing
Connect your LEDs to the PWM output of the Raspberry Pi and start the program:

* `sudo ./ws2812svr`

Now first initialize the driver code from jgarff by typing 'setup'.
On the following line you must **replace 10** by the number of leds you have attached!.

* `setup 1,10`
* `init`

Now you can type commands to change the color of the leds.
For example make them all red:

* `fill 1,FF0000`
* `render`

# Supported commands
Here is a list of commands you can type or send to the program. All commands have optional comma seperated parameters. The parameters must be in the correct order!


* `init` command must be called everytime the program is started after the setup command, this will initialize resource on the Pi according to the setup command
```
init  
    init
		<frequency>, 					#Frequency to use for communication to the LEDs, default 800000
		<dma> 					        #dma channel number to use, default 10 be careful not to use any channels in use by the system as this may crash your SD card/OS
									    #use cat /proc/device-tree/soc/dma@7e007000/brcm,dma-channel-mask to see which channels (bitmask) might be used by the OS
```

* `setup` command must be called everytime the program is started:
```
setup  
    setup
		<channel>, 						#channel number
		<led_count>, 					#number of leds in channel
		<led_type>, 					#type of led (3 color or 4 color) default 0
		<invert>, 						#invert output, default 0
		<global_brightness>, 			#global brightness level for channel (0-255), default 255
		<gpionum>						#GPIO output number, default 18 for more see 'GPIO usage' at this page: [https://github.com/jgarff/rpi_ws281x](https://github.com/jgarff/rpi_ws281x)

	Possible LED types:
		0 WS2811_STRIP_RGB
		1  WS2811_STRIP_RBG
		2  WS2811_STRIP_GRB
		3  WS2811_STRIP_GBR
		4  WS2811_STRIP_BRG
		5  WS2811_STRIP_BGR
		6  SK6812_STRIP_RGBW
		7  SK6812_STRIP_RBGW
		8  SK6812_STRIP_GRBW
		9  SK6812_STRIP_GBRW
		10 SK6812_STRIP_BRGW
		11 SK6812_STRIP_BGRW

    Example:
    setup 1,10,0
```

* `render` command sends the internal buffer to all leds
```
render   
    <channel>,          #send the internal color buffer to all the LEDS of <channel> default is 1  
    <start>,            #before render change the color of led(s) beginning at <start> (0=led 1)  
    <RRGGBBRRGGBB...>   #color to change the led at start Red+green+blue (no default)  
```

* `rotate` command moves all color values of 1 channel
```
rotate  
    <channel>,         #channel to rotate (default 1)  
    <places>,          #number of places to move each color value (default 1)  
    <direction>,       #direction (0 or 1) for forward and backwards rotating (default 0)  
    <RRGGBB>           #first led(s) get this color instead of the color of the last led  
```

* `rainbow` command creates rainbows or gradient fills
```
rainbow  
    <channel>,         #channel to fill with a gradient/rainbow (default 1)  
    <count>,           #number of times to repeat the rainbow in the channel (default 1)  
    <start_color>,     #color to start with value from 0-255 where 0 is red and 255 pink (default is 0)  
    <end_color>,       #color to end with value from 0-255 where 0 is red and 255 pink (default 255)
	<start>,		   #start at this led position
	<len>			   #number of leds to change
```

* `fill` command fills number of leds with a color value
```
fill  
    <channel>,          	#channel to fill leds with color (default 1)  
    <RRGGBB>,           	#color to fill (default FF0000)  
    <sections>,        		#multiple sections ("<form_led>-<to_led>") or single leds ("<led>") septerated by colon. (e.g. 1-3:6-7:8:10-12)
	<OR,AND,XOR,NOT,=>	#bitwise operator to execute on OLD and NEW color, default = copies new color to output
```

* `delay` command waits for number of milliseconds
```
delay  
    <milliseconds>      #enter number of milliseconds to wait	  
```

* `brightness` command changes the brightness of a single or multiple leds without changing the actual color value
```
	brightness
		<channel>,		#channel number to change brightness (default 1)
		<brightness>,	 	#brightness to set (0-255, default 255)
		<sections>,        	#multiple sections ("<form_led>-<to_led>") or single leds ("<led>") septerated by colon. (e.g. 1-3:6-7:8:10-12)
```

* `fade` command changes the brightness over time
```
	fade
		<channel>,					 #channel to fade
		<start_brightness>,				 #start brightness (default 0)
		<end_brightness>,				 #end brightness (default 255)
		<delay ms>,					 #delay in ms
		<step>,						 #step to increase / decrease brightness every delay untill end_brightness is reached
		<start_led>,					 #start led
		<len>						 #number of leds to change starting at start (default is channel count)
```

* `gradient` command makes a smooth change of color or brightness level in a channel
```
	gradient
		<channel>,					 #channel number to change
		<RGBWL>,					 #which color component to change, R = red, G = green, B = blue, W = white and L = brightness level
		<start_level>,					 #start at color level (0-255) default is 0
		<end_level>, 					 #end at color level (0-255) default is 255
		<start_led>,					 #start at led number (default is 0)
		<len>						 #number of leds to change (default is channel count)
```

* `random` command can create a random color
```
	random
		<channel>,					#channel number to change
		<start>,					#start at this led
		<len>,						#number of leds to fill with a random color, default is channel count
		<RGBWL>						#color to use in random can be R = red, G = green, B = blue, W = White, L = brightness also combination is possible like RGBW or RL
```

* `readjpg` command can read the pixels from a JPEG file and fill them into the LEDs of a channel
```
    readjpg
    	<channel>,						#channel number to load pixels to
		<FILE>,							#file location of the JPG without any "" cannot contain a ,
		<start>,						#start position, start loading at this LED in channel (default 0)
		<len>,							#load this ammount of pixel/LEDs	(default is channel count or led count)
		<offset>,						#start at pixel offset in JPG file (default is 0)
		<OR AND XOR NOT =>,				#operator to use, use NOT to reverse image (default is =)
		<delay>							#optional argument the delay between rendering next scan line in the jpg file, if 0 only first line is loaded in to memory and no render performed. default 0
```

* `readpng` command can read the pixels from a PNG file and fill them into the LEDs of a channel
```
	readpng
		<channel>,						#channel number to load pixels to
		<FILE>,							#file location of the PNG file without any "" cannot contain a ,
		<BACKCOLOR>,					#the color to use for background in case of a transparent image
										#(default is the PNG image backcolor = P), if BACKCOLOR = W the alpha channel will be used for the W in RGBW LED strips
		<start>,						#start position, start loading at this LED in channel (default 0)
		<len>,							#load this ammount of pixel/LEDs	(default is channel count or led count)
		<offset>,						#start at pixel offset in JPG file (default is 0)
		<OR AND XOR =>,					#operator to use, use NOT to reverse image (default is =)
		<delay>							#optional argument the delay between rendering next scan line in the png file, if 0 only first line is loaded in to memory and no render performed. default 0
```

* `blink` command makes a group of leds blink between 2 given colors
```
	blink
		<channel>,					#channel number to change
		<color1>,					#first color to use
		<color2>,					#second color
		<delay>,					#delay in ms between change from color1 to color2
		<blink_count>,					#number of changes between color1 and color2
		<sections>,        				#multiple sections ("<form_led>-<to_led>") or single leds ("<led>") septerated by colon. (e.g. 1-3:6-7:8:10-12)
```

* `gpio` command set state of gpio ([wiringPi pinnums](https://pinout.xyz/pinout/wiringpi)) ATTENTION: Changes slected gpio to output
```
gpio
    <pin>,         	#gpio number ([wiringPi](https://pinout.xyz/pinout/wiringpi))
    <state>        	#state could be 0 (=off) or 1(=on)
```

* `random_fade_in_out` creates some kind of random blinking/fading leds effect
```
	random_fade_in_out
		<channel>,						#channel number to use
		<duration Sec>,					#total max duration of effect
		<count>,						#max number of leds that will fade in or out at same time
		<delay>,						# delay between changes in brightness
		<step>,							#ammount of brightness to increase/decrease between delays
		<inc_dec>,						#inc_dec = if 1 brightness will start at <brightness> and decrease to initial brightness of the led, else it will start low and go up
		<brightness>,					#brightness to start with when blinking starts
		<start>,						#start position
		<len>,							#number of leds
		<color>							#color to use for blinking leds

Try this as an example for a 300 LED string:
  fill 1,FFFFFF;
  brightness 1,0;
  random_fade_in_out 1,60,50,10,15,800;
```

* `color_change` slowly change all leds from one color to another
```
	color_change
		<channel>,						#channel number to use
		<start_color>,					#color to start with value from 0-255 where 0 is red and 255 pink (default is 0)
		<stop_color>,					#color to end with value from 0-255 where 0 is red and 254 pink (default is 255)
		<duration>,						#total number of ms event should take, default is 10 seconds
		<start>,						#start effect at this led position
		<len>							#number of leds to change starting at start
```

* `chaser` makes a chaser light
```
	chaser
		<channel>,					#channel number to use
		<duration>,					#max number of seconds the event may take in seconds (default 10) use 0 to make chaser run forever
		<color>,						#color 000000-FFFFFF to use for chasing leds
    <size>,             #number of leds that are used as chaser
		<direction>,				#direction 1 or 0 to indicate forward/backwards direction of movement
		<delay>,						#delay between moving one pixel (milliseconds) default is 10ms
		<start>,						#start effect at this led position
		<len>,							#number of leds to change starting at start
		<brightness>,				#brightness value of chasing leds (0-255) default is 255
		<loops>							#max number of loops, use 0 to loop forever / duration time
```


* `fly_in` fill entire string with given brightness level, moving leds from left/right untill all leds have brightness level or a given color
```
	fly_in
		<channel>,						#channel number to use
		<direction>,					#direction where to start with fly in effect (default 1)
		<delay>,						#delay between moving pixels in ms (default 10ms)
		<brightness>,					#final brightness of all leds default 255 or full ON
		<start>,						#start effect at this led position
		<len>,							#number of leds to change starting at start
		<start_brightness>,				#at beginning give all leds this brightness value
		<color>							#final color of the leds default is to use the current color
	NOTICE: first fill entire strip with a color if leaving color argument default (use fill <channel>,<color>)
```

* `fly_out` fill entire string with given brightness level, moving leds from left/right untill all leds have brightness level or a given color
```
	fly_out
		<channel>,						#channel number to use
		<direction>,					#direction where to start with fly out effect (default 1)
		<delay>,						#delay between moving pixels in ms (default 10ms)
		<brightness>,					#brightness of led that is moving out default is 255
		<start>,						#start effect at this led position
		<len>,							#number of leds to change starting at start
		<end_brightness>,				#brightness of all leds at end, default is 0 = OFF
		<color>							#final color of the leds default is to use the current color
	NOTICE: first fill entire strip with a color before calling this function (use fill <channel>,<color>)
```

# Special keywords
You can add `do ... loop` to repeat commands when using a file or TCP connection.

For example the commands between `do` and `loop` will be executed 10 times:
```
do   
   <enter commands here to repeat>    
loop 10
```
Endless loops can be made by removing the '10'.
Inside a loop you can use {i} for the loop counter as function argument where i is the loop index (for loop inside loop)
For example {0} will be automatically replace by 0,1,2,3,4:

```
do
	fill 1, FF0000, {0}, 1
loop 5
render
```

is the same as the C-style code:

```
for (i=0;i<5;i++){
	fill 1, FF0000, i, 1
}
```

If you have nested loops you can increase the {0} to {1}, {2},...

```
do
	do
		fill 1, FF0000, {1}, 1
	loop 5
	render
loop
```

Also possible to add a step value for the loop index to fill every "even" led

```
do
	fill 1, FF0000, {0}, 1
loop 5,2
```

is the same as:

```
for (i=0;i<5;i+=2){
	fill 1, FF0000, i, 1
}
```

To create an alternating pattern of colors use rotate commands in a loop.
For a 300 LED string this will create alternating RED-YELLOW-GREEN-BLUE-PINK colors:
```
do
    rotate 1,1,1,FF0000
    rotate 1,1,1,FFFF00
    rotate 1,1,1,00FF00
    rotate 1,1,1,0000FF
	rotate 1,1,1,FF00FF
loop 60
```

For `do ... loop` to work from a TCP connection we must start a new thread.
This thread will continue to execute the commands when the client disconnects from the TCP/IP connection.
The thread will automatically stop executing the next time the client reconnects (ideal for webservers).

For example:
```
thread_start   
   do  
      rotate 1,1,2  
     render  
     delay 200  
  loop  
thread_stop  
<client must close connection now>   
```

# PHP example
First start the server program:

* `sudo ./ws2812svr -tcp`

Then run the php code from the webserver:

```PHP
//create a rainbow for 10 leds on channel 1:  
send_to_leds("setup 1,10;init;brightness 1,32;");  
function send_to_leds ($data){  
   $sock = fsockopen("127.0.0.1", 9999);  
   fwrite($sock, $data);  
   fclose($sock);  
}
```

# Command line parameters
* `sudo ./ws2812svr -tcp 9999`
  Listens for clients to connect to port 9999 (default).
* `sudo ./ws2812svr -f text_file.txt`
  Loads commands from text_file.txt.
* `sudo ./ws2812svr -p /dev/ws281x`
  Creates a file called `/dev/ws281x` where you can write you commands to with any other programming language (do-loop not supported here).
* `sudo ./ws2812svr -i "setup 1,4,5;init;"`
  Initializes with command setup 1,4,5  and command init
* `sudo ./ws2812svr -c /etc/ws2812svr.conf`
  Loads with settings from /etc/ws2812svr.conf

# Run Art-Net
First start ws2812sv listening on tcp port 9999. Then run `run.sh` in located in subfolder hcu-artnet-server

# Running as a service
To run as service run `nano hcu-artnet-server/run.sh` and change the path to <INSTALL_DIR>/hcu-artnet-server (default is /home/pi/git/rpi-ws2812-server/hcu-artnet-server). Run make install after compilation and adjust the config file in /etc/ws2812svr.conf
```
make
sudo make install
```

After installing service it will run the artnet-bridge that connects to ws2812svr. ws2812svr runs by default in TCP mode on port 9999, if you want to change this you must edit the config file:
```
sudo nano /etc/ws2812svr.conf
```
change the mode to:
tcp for TCP mode (change the port= setting)
file for file mode (change the file= setting for location of file)
pipe for named pipe mode (change the pipe= setting for the location of the named pipe)
mode must be first setting in the conf file!
init setting can be used to initialize the led count and type fill color,...
```
mode=tcp
port=9999
file=/home/pi/test.txt
pipe=/dev/leds
init=
```


# Complicated animations
If you need to create complicated animations I suggest to save the color values (each led 1 pixel) in a png or jpg image file and load this file with the readpng command.
If you have a LED string of 300 leds best is to create an image file which is 300 pixels wide and X pixels high.
