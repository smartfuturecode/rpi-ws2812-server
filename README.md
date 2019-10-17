# Rpi-ws2812-server
This is a small program for driving the WS281x (a.k.a. [NeoPixel](https://www.sparkfun.com/products/12999)) LEDs from a webserver, command line, text file, Python,... using the Raspberry Pi. It uses the rpi_ws281x PWM driver code from jgarff ([https://github.com/jgarff/rpi_ws281x](https://github.com/jgarff/rpi_ws281x)). The LEDs can be controlled by sending text commands to a tcp socket. These commands can be generated by a webserver script, android app,... It's also possible to control the leds directly from the command line or by loading text file containing some predefined color patterns.

# Installation
On the raspberry you open a terminal window and type following commands:
* `sudo apt-get update`
* `sudo apt-get install gcc make git libjpeg-dev libpng-dev wiringpi`
* `git clone https://github.com/smartfuturecode/rpi-ws2812-server.git`
* `cd rpi-ws2812-server`
* `make`
* `sudo chmod +x ws2812svr`

Newer versions require libjpeg-dev and libpng-dev for reading PNG and JPEG images.
If you don't want to use JPEG or PNG you can disable this using:
* `make NO_JPEG=1 NO_PNG=1`

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
    <end_color>        #color to end with value from 0-255 where 0 is red and 255 pink (default 255)  
```

* `fill` command fills number of leds with a color value
```
fill  
    <channel>,          	#channel to fill leds with color (default 1)  
    <RRGGBB>,           	#color to fill (default FF0000)  
    <sections>,        		#multiple sections ("<form_led>-<to_led>") septeratet by blank spaces. Please make sure, that the sections aren't overlaped by each other. (e.g. 1-3 6-7 8-8)
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
		<channel>,		 #channel number to change brightness (default 1)
		<brightness>,	 	#brightness to set (0-255, default 255)
		<start>,		 #start at this led number (default 0)
		<len>			 #number of leds to change starting at start (default led count of channel)
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
		<FILE>,						#file location of the JPG without any "" cannot contain a ,
		<start>,					#start position, start loading at this LED in channel (default 0)
		<len>,						#load this ammount of pixel/LEDs	(default is channel count or led count)
		<offset>,					#start at pixel offset in JPG file (default is 0)
		<OR AND XOR NOT =>				#operator to use, use NOT to reverse image (default is =)
```

* `readpng` command can read the pixels from a PNG file and fill them into the LEDs of a channel
```
	readpng
		<channel>,					#channel number to load pixels to
		<FILE>,						#file location of the PNG file without any "" cannot contain a ,
		<BACKCOLOR>,					#the color to use for background in case of a transparent image 
								#(default is the PNG image backcolor = P), if BACKCOLOR = W the alpha channel will be used for the W in RGBW LED strips
		<start>,					#start position, start loading at this LED in channel (default 0)
		<len>,						#load this ammount of pixel/LEDs	(default is channel count or led count)
		<offset>,					#start at pixel offset in JPG file (default is 0)
		<OR AND XOR =>					#operator to use, use NOT to reverse image (default is =)
```

* `blink` command makes a group of leds blink between 2 given colors
```
	blink
		<channel>,					#channel number to change
		<color1>,					#first color to use
		<color2>,					#second color
		<delay>,					#delay in ms between change from color1 to color2
		<blink_count>,					#number of changes between color1 and color2
		<startled>,					#start at this led position
		<len>						#number of LEDs to blink starting at startled
```

* `gpio` command set state of gpio
```
gpio  
    <pin>,         	#gpio number (wiringPi: https://pinout.xyz/pinout/wiringpi) 
    <state>        	#state could be 0 (=off) or 1(=on)
```

# Special keywords
You can add `do ... loop` to repeat commands when using a file or TCP connection.

For example the commands between `do` and `loop` will be executed 10 times:
```
do   
   <enter commands here to repeat>    
loop 10
```
(Endless loops can be made by removing the '10')

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
send_to_leds("setup channel_1_count=10;rainbow;brightness 1,32;");  
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
