#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include "ws2811.h"

#define DEFAULT_DEVICE_FILE "/dev/ws281x"
#define DEFAULT_COMMAND_LINE_SIZE 1024
#define DEFAULT_BUFFER_SIZE 32768

#define MAX_KEY_LEN 255
#define MAX_VAL_LEN 255
#define MAX_LOOPS 32

#define MODE_STDIN 0
#define MODE_NAMED_PIPE 1
#define MODE_FILE 2
#define MODE_TCP 3

#define ERROR_INVALID_CHANNEL "Invalid channel number, did you call setup and init?\n"

//USE_JPEG and USE_PNG is enabled through make file
//to disable compilation of PNG / or JPEG use:
//make PNG=0 JPEG=0
//#define USE_JPEG 1
//#define USE_PNG 1

#ifdef USE_JPEG
	#include "jpeglib.h"
	#include <setjmp.h>
	//from https://github.com/LuaDist/libjpeg/blob/master/example.c
	struct my_error_mgr {
	  struct jpeg_error_mgr pub;	/* "public" fields */

	  jmp_buf setjmp_buffer;	/* for return to caller */
	};

	typedef struct my_error_mgr * my_error_ptr;

	/*
	 * Here's the routine that will replace the standard error_exit method:
	 */

	METHODDEF(void)
	my_error_exit (j_common_ptr cinfo)
	{
	  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	  my_error_ptr myerr = (my_error_ptr) cinfo->err;

	  /* Always display the message. */
	  /* We could postpone this until after returning, if we chose. */
	  (*cinfo->err->output_message) (cinfo);

	  /* Return control to the setjmp point */
	  longjmp(myerr->setjmp_buffer, 1);
	}
#endif

#ifdef USE_PNG
	#include "readpng.h"
#endif

//for easy and fast converting asci hex to integer
char hextable[256] = {
   [0 ... 255] = 0, // bit aligned access into this table is considerably
   ['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, // faster for most modern processors,
   ['A'] = 10, 11, 12, 13, 14, 15,       // for the space conscious, reduce to
   ['a'] = 10, 11, 12, 13, 14, 15        // signed char.
};

/*void init_hextable(){
    unsigned int i;
    for(i=0;i<256;i++){
        if (i >= '0' && i<= '9') hextable[i]=i-'0';
        if (i >= 'a' && i<= 'f') hextable[i]=i-'a'+10;
        if (i >= 'A' && i<= 'F') hextable[i]=i-'A'+10;
    }
}*/

typedef struct {
    int do_pos;
    int n_loops;
} do_loop;


FILE *    input_file;         //the named pipe handle
char *    command_line;       //current command line
char *    named_pipe_file;    //holds named pipe file name 
char *    initialize_cmd=NULL; //initialze command
int       command_index;      //current position
int       command_line_size;  //max bytes in command line
int       exit_program=0;     //set to 1 to exit the program
int       mode;               //mode we operate in (TCP, named pipe, file, stdin)
do_loop   loops[MAX_LOOPS]={0};      //positions of 'do' in file loop, max 32 recursive loops
int       loop_index=0;       //current loop index
int       debug=0;            //set to 1 to enable debug output

//for TCP mode
int sockfd;        //socket that listens
int active_socket; //current active connection socket
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;

//for TCP/IP multithreading
char *       thread_data=NULL;         //holds command to execute in separate thread (TCP/IP only)
int          thread_read_index=0;      //read position 
int          thread_write_index=0;     //write position
int          thread_data_size=0;       //buffer size
volatile int thread_running=0;         //becomes 1 there is a thread running
int          write_to_thread_buffer=0; //becomes 1 if we need to write to thread buffer
int          start_thread=0;           //becomes 1 after the thread_stop command and tells the program to start the thread on disconnect of the TCP/IP connection
//pthread_mutex_t mutex_fifo_queue; 
pthread_t thread; //a thread that will repeat code after client closed connection

ws2811_t ledstring;

void process_character(char c);

//handles exit of program with CTRL+C
static void ctrl_c_handler(int signum){
	exit_program=1;
}

static void setup_handlers(void){
    struct sigaction sa;
    sa.sa_handler = ctrl_c_handler,
    sigaction(SIGKILL, &sa, NULL);
}

//allocates memory for command line
void malloc_command_line(int size){
	if (command_line!=NULL) free(command_line);
	command_line = (char *) malloc(size+1);
	command_line_size = size;
	command_index = 0;
}

unsigned char get_red(int color){
	return color & 0xFF;
}

unsigned char get_green(int color){
	return (color >> 8) & 0xFF;
}

unsigned char get_blue(int color){
	return (color >> 16) & 0xFF;
}

unsigned char get_white(int color){
	return (color >> 24) & 0xFF;
}

//returns a color from RGB value
//note that the ws281x stores colors as GRB
int color (unsigned char r, unsigned char g, unsigned char b){
	return (b << 16) + (g << 8) + r;
}

//returns new colorcomponent based on alpha number for component1 and background component
//all values are unsigned char 0-255
unsigned char alpha_component(unsigned int component, unsigned int bgcomponent, unsigned int alpha){
	return component * alpha / 255 + bgcomponent * (255-alpha)/255;
}

//returns a color from RGBW value
//note that the ws281x stores colors as GRB(W)
int color_rgbw (unsigned char r, unsigned char g, unsigned char b, unsigned char w){
	return (w<<24)+(b << 16) + (g << 8) + r;
}

//returns a color from a 'color wheel' where wheelpos is the 'angle' 0-255
int deg2color(unsigned char WheelPos) {
	if(WheelPos < 85) {
		return color(255 - WheelPos * 3,WheelPos * 3 , 0);
	} else if(WheelPos < 170) {
		WheelPos -= 85;
		return color(0, 255 - WheelPos * 3, WheelPos * 3);
	} else {
		WheelPos -= 170;
		return color(WheelPos * 3, 0, 255 - WheelPos * 3);
	}
}

//returns if channel is a valid led_string index number
int is_valid_channel_number(unsigned int channel){
    return (channel >= 0) && (channel < RPI_PWM_CHANNELS) && ledstring.channel[channel].count>0 && ledstring.device!=NULL;
}

//reads key from argument buffer
//example: channel_1_count=10,
//returns channel_1_count in key buffer, then use read_val to read the 10
char * read_key(char * args, char * key, size_t size){
	if (args!=NULL && *args!=0){
		size--;
		if (*args==',') args++;
		while (*args!=0 && *args!='=' && *args!=','){
			if (*args!=' ' && *args!='\t'){ //skip space
				*key=*args; //add character to key value
				key++;
				size--;
				if (size==0) break;
			}
			args++;
		}
		*key=0;
	}
	return args;
}

//read value from command argument buffer (see read_key)
char * read_val(char * args, char * value, size_t size){
	if (args!=NULL && *args!=0){
		size--;
		if (*args==',') args++;
		while (*args!=0 && *args!=','){
			if (*args!=' ' && *args!='\t'){ //skip space
				*value=*args;
				value++;
				size--;
				if (size==0) break;
			}
			args++;
		}
		*value=0;
	}
	return args;
}

//reads integer from command argument buffer
char * read_int(char * args, int * value){
	char svalue[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, svalue, MAX_VAL_LEN);
		*value = atoi(svalue);
	}
	return args;
}

char * read_str(char * args, char * dst, size_t size){
	return read_val(args, dst, size);
}

//reads unsigned integer from command argument buffer
char * read_uint(char * args, unsigned int * value){
	char svalue[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, svalue, MAX_VAL_LEN);
		*value = (unsigned int) (strtoul(svalue,NULL, 10) & 0xFFFFFFFF);
	}
	return args;	
}

char * read_channel(char * args, int * value){
	if (args!=NULL && *args!=0){
		args = read_int(args, value);
		(*value)--;
	}
	return args;
}

//reads color from string, returns string + 6 or 8 characters
//color_size = 3 (RGB format)  or 4 (RGBW format)
char * read_color(char * args, unsigned int * out_color, unsigned int color_size){
    unsigned char r,g,b,w;
    unsigned char color_string[8];
    unsigned int color_string_idx=0;
	if (args!=NULL && *args!=0){
		*out_color = 0;
		while (*args!=0 && color_string_idx<color_size*2){
			if (*args!=' ' && *args!='\t'){ //skip space
				color_string[color_string_idx]=*args;
				color_string_idx++;
			}
			args++;
		}
		
		r = (hextable[color_string[0]]<<4) + hextable[color_string[1]];
		g = (hextable[color_string[2]]<<4) + hextable[color_string[3]];
		b = (hextable[color_string[4]]<<4) + hextable[color_string[5]];
		if (color_size==4){
			w = (hextable[color_string[6]]<<4) + hextable[color_string[7]];
			*out_color = color_rgbw(r,g,b,w);
		}else{
			*out_color = color(r,g,b);
		}
	}
    return args;
}

char * read_color_arg(char * args, unsigned int * out_color, unsigned int color_size){
	char value[MAX_VAL_LEN];
	args = read_val(args, value, MAX_VAL_LEN);
	if (*value!=0) read_color(value, out_color, color_size);
	return args;
}

//reads a hex brightness value
char * read_brightness(char * args, unsigned int * brightness){
    unsigned int idx=0;
    unsigned char str_brightness[2];
	if (args!=NULL && *args!=0){
		*brightness=0;
		while (*args!=0 && idx<2){
			if (*args!=' ' && *args!='\t'){ //skip space
				brightness[idx]=*args;
				idx++;
			}
			args++;
		}
		* brightness = (hextable[str_brightness[0]] << 4) + hextable[str_brightness[1]];
	}
    return args;
}

#define OP_EQUAL 0
#define OP_OR 1
#define OP_AND 2
#define OP_XOR 3
#define OP_NOT 4

char * read_operation(char * args, char * op){
	char value[MAX_VAL_LEN];
	if (args!=NULL && *args!=0){
		args = read_val(args, value, MAX_VAL_LEN);
		if (strcmp(value, "OR")==0) *op=OP_OR;
		else if (strcmp(value, "AND")==0) *op=OP_AND;
		else if (strcmp(value, "XOR")==0) *op=OP_XOR;
		else if (strcmp(value, "NOT")==0) *op=OP_NOT;
		else if (strcmp(value, "=")==0) *op=OP_EQUAL;
	}	
	return args;
}

//returns time stamp in ms
unsigned long long time_ms(){
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

//initializes channels
//init <frequency>,<DMA>
void init_channels(char * args){
    char value[MAX_VAL_LEN];
    int frequency=WS2811_TARGET_FREQ, dma=10;
    
    if (ledstring.device!=NULL)	ws2811_fini(&ledstring);
    
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
        frequency=atoi(value);
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            dma=atoi(value);
        }
    }
    
    ledstring.dmanum=dma;
    ledstring.freq=frequency;
    if (debug) printf("Init ws2811 %d,%d\n", frequency, dma);
    ws2811_return_t ret;
    if ((ret = ws2811_init(&ledstring))!= WS2811_SUCCESS){
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    }
}

//changes the global channel brightness
//global_brightness <channel>,<value>
void global_brightness(char * args){
    if (args!=NULL){
        char value[MAX_VAL_LEN];
        args = read_val(args, value, MAX_VAL_LEN);
        int channel = atoi(value)-1;
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
            int brightness = atoi(value);
            if (is_valid_channel_number(channel)){
                ledstring.channel[channel].brightness=brightness;
                if(debug) printf("Global brightness %d, %d\n", channel, brightness);
            }else{
                fprintf(stderr,ERROR_INVALID_CHANNEL);
            }
        }
    }
}

//sets the ws2811 channels
//setup channel, led_count, type, invert, global_brightness, GPIO
void setup_ledstring(char * args){
    int channel=0, led_count=10, type=0, invert=0, brightness=255, GPIO=18;
	
    const int led_types[]={WS2811_STRIP_RGB, //0 
                           WS2811_STRIP_RBG, //1 
                           WS2811_STRIP_GRB, //2 
                           WS2811_STRIP_GBR, //3 
                           WS2811_STRIP_BRG, //4
                           WS2811_STRIP_BGR, //5 
                           SK6812_STRIP_RGBW,//6
                           SK6812_STRIP_RBGW,//7
                           SK6812_STRIP_GRBW,//8 
                           SK6812_STRIP_GBRW,//9 
                           SK6812_STRIP_BRGW,//10 
                           SK6812_STRIP_BGRW //11
                           };
    
	args = read_channel(args, & channel);
	args = read_int(args, & led_count);
	args = read_int(args, & type);
	args = read_int(args, & invert);
	args = read_int(args, & brightness);
	args = read_int(args, & GPIO);
    
    if (channel >=0 && channel < RPI_PWM_CHANNELS){

        if (debug) printf("Initialize channel %d,%d,%d,%d,%d,%d\n", channel, led_count, type, invert, brightness, GPIO);

        int color_size = 4;       
        
        switch (led_types[type]){
            case WS2811_STRIP_RGB:
            case WS2811_STRIP_RBG:
            case WS2811_STRIP_GRB:
            case WS2811_STRIP_GBR:
            case WS2811_STRIP_BRG:
            case WS2811_STRIP_BGR:
                color_size=3;
                break;
        }           
        
        ledstring.channel[channel].gpionum = GPIO;
        ledstring.channel[channel].invert = invert;
        ledstring.channel[channel].count = led_count;
        ledstring.channel[channel].strip_type=led_types[type];
        ledstring.channel[channel].brightness=brightness;
        ledstring.channel[channel].color_size=color_size;

        int max_size=0,i;
        for (i=0; i<RPI_PWM_CHANNELS;i++){
            int size = DEFAULT_COMMAND_LINE_SIZE + ledstring.channel[i].count * 2 * ledstring.channel[i].color_size;
            if (size > max_size){
                max_size = size;
            }
        }
        malloc_command_line(max_size); //allocate memory for full render data    
    }else{
        if (debug) printf("Channel number %d\n", channel);
        fprintf(stderr,"Invalid channel number, use channels <number> to initialize total channels you want to use.\n");
    }
}

//prints channel settings
void print_settings(){
    unsigned int i;
    printf("DMA Freq:   %d\n", ledstring.freq); 
    printf("DMA Num:    %d\n", ledstring.dmanum);    
    for (i=0;i<RPI_PWM_CHANNELS;i++){
        printf("Channel %d:\n", i+1);
        printf("    GPIO: %d\n", ledstring.channel[i].gpionum);
        printf("    Invert: %d\n",ledstring.channel[i].invert);
        printf("    Count:  %d\n",ledstring.channel[i].count);
        printf("    Colors: %d\n", ledstring.channel[i].color_size);
        printf("    Type:   %d\n", ledstring.channel[i].strip_type);
    }
}

//sends the buffer to the leds
//render <channel>,0,AABBCCDDEEFF...
//optional the colors for leds:
//AABBCC are RGB colors for first led
//DDEEFF is RGB for second led,...
void render(char * args){
	int channel=0;
	int r,g,b,w;
	int size;
    int start;
    char color_string[6];
    
	if (debug) printf("Render %s\n", args);
	
    if (args!=NULL){
		args = read_channel(args, & channel); //read_val(args, & channel, MAX_VAL_LEN);
		//channel = channel-1;
        if (is_valid_channel_number(channel)){
            if (*args!=0){
                args = read_int(args, & start); //read start position
                while (*args!=0 && (*args==' ' || *args==',')) args++; //skip white space
                
                if (debug) printf("Render channel %d selected start at %d leds %d\n", channel, start, ledstring.channel[channel].count);
                
                size = strlen(args);
                int led_count = ledstring.channel[channel].count;            
                int led_index = start % led_count;
                int color_count = ledstring.channel[channel].color_size;
                ws2811_led_t * leds = ledstring.channel[channel].leds;

                while (*args!=0){
                    unsigned int color=0;
                    args = read_color(args, & color, color_count);
                    leds[led_index].color = color;
                    led_index++;
                    if (led_index>=led_count) led_index=0;
                }
            }			
        }
	}
	if (is_valid_channel_number(channel)){
		ws2811_render(&ledstring);
	}else{
		fprintf(stderr,ERROR_INVALID_CHANNEL);
	}
}

void rotate_strip(int channel, int nplaces, int direction, unsigned int new_color, int use_new_color, int new_brightness){
	ws2811_led_t tmp_led;
    ws2811_led_t * leds = ledstring.channel[channel].leds;
    unsigned int led_count = ledstring.channel[channel].count;
	unsigned int n,i;
	for(n=0;n<nplaces;n++){
		if (direction==1){
			tmp_led = leds[0];
			for(i=1;i<led_count;i++){
				leds[i-1] = leds[i]; 
			}
			if (use_new_color){
				leds[led_count-1].color=new_color;
				leds[led_count-1].brightness=new_brightness;
			}else{
				leds[led_count-1]=tmp_led;
			}
		}else{
			tmp_led = leds[led_count-1];
			for(i=led_count-1;i>0;i--){
				leds[i] = leds[i-1]; 
			}
			if (use_new_color){
				leds[0].color=new_color;	
				leds[0].brightness=new_brightness;
			}else{
				leds[0]=tmp_led;		
			}
		}
	}	
}

//shifts all colors 1 position
//rotate <channel>,<places>,<direction>,<new_color>,<new_brightness>
//if new color is set then the last led will have this color instead of the color of the first led
void rotate(char * args){
	int channel=0, nplaces=1, direction=1;
    unsigned int new_color=0, new_brightness=255;
	int use_new_color=0;
    
	args = read_channel(args, & channel);
	args = read_int(args, & nplaces);
	args = read_int(args, & direction);
	if (is_valid_channel_number(channel)){
		use_new_color= (args!=NULL && *args!=0);
		args = read_color_arg(args, & new_color, ledstring.channel[channel].color_size);
		read_brightness(args, & new_brightness);
	}
	
	if (debug) printf("Rotate %d %d %d %d %d\n", channel, nplaces, direction, new_color, new_brightness);
	
    if (is_valid_channel_number(channel)){
		rotate_strip(channel, nplaces, direction, new_color, use_new_color, new_brightness);
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//fills pixels with rainbow effect
//count tells how many rainbows you want
//rainbow <channel>,<count>,<startcolor>,<stopcolor>,<start>,<len>
//start and stop = color values on color wheel (0-255)
void rainbow(char * args) {
	int channel=0, count=1,start=0,stop=255,startled=0, len=0;
	
    if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & count);
	args = read_int(args, & start);
	args = read_int(args, & stop);
	args = read_int(args, & startled);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){
        if (start<0 || start > 255) start=0;
        if (stop<0 || stop > 255) stop = 255;
        if (startled<0) startled=0;
        if (startled+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-startled;
        
        if (debug) printf("Rainbow %d,%d,%d,%d,%d,%d\n", channel, count,start,stop,startled,len);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        for(i=0; i<numPixels; i++) {
            leds[startled+i].color = deg2color(abs(stop-start) * i * count / numPixels + start);
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}



//fills leds with certain color
//fill <channel>,<color>,<start>,<len>,<OR,AND,XOR,NOT,=>
void fill(char * args){
    char op=0;
	int channel=0,start=0,len=-1;
	unsigned int fill_color=0;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & fill_color, ledstring.channel[channel].color_size);
    args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_operation(args, & op);

	
	if (is_valid_channel_number(channel)){
        if (start<0 || start>=ledstring.channel[channel].count) start=0;        
        if (len<=0 || (start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;

        if (debug) printf("fill %d,%d,%d,%d,%d\n", channel, fill_color, start, len,op);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        unsigned int i;
        for (i=start;i<start+len;i++){
            switch (op){
                case OP_EQUAL:
                    leds[i].color=fill_color;
                    break;
                case OP_OR:
                    leds[i].color|=fill_color;
                    break;
                case OP_AND:
                    leds[i].color&=fill_color;
                    break;
                case OP_XOR:
                    leds[i].color^=fill_color;
                    break;
                case OP_NOT:
                    leds[i].color=~leds[i].color;
                    break;
            }
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//dims leds
//brightness <channel>,<brightness>,<start>,<len> (brightness: 0-255)
void brightness(char * args){
	int channel=0, brightness=255;
	unsigned int start=0, len=0;
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len = ledstring.channel[channel].count;;
	args = read_int(args, & brightness);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
	
	if (is_valid_channel_number(channel)){
        if (brightness<0 || brightness>0xFF) brightness=255;
        
        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (debug) printf("Changing brightness %d, %d, %d, %d\n", channel, brightness, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        unsigned int i;
        for (i=start;i<start+len;i++){
            leds[i].brightness=brightness;
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//causes a fade effect in time
//fade <channel>,<startbrightness>,<endbrightness>,<delay>,<step>,<startled>,<len>
void fade (char * args){
	int channel=0, brightness=255,step=1,startbrightness=0, endbrightness=255;
	unsigned int start=0, len=0, delay=50;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	args = read_int(args, & startbrightness);
	args = read_int(args, & endbrightness);
	args = read_int(args, & delay);
	args = read_int(args, & step);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
            
	if (is_valid_channel_number(channel)){
        if (startbrightness>0xFF) startbrightness=255;
        if (endbrightness>0xFF) endbrightness=255;
        
        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (step==0) step = 1;
        if (startbrightness>endbrightness){ //swap
            if (step > 0) step = -step;
        }else{
            if (step < 0) step = -step;
        }        
        
        if (debug) printf("fade %d, %d, %d, %d, %d, %d, %d\n", channel, startbrightness, endbrightness, delay, step,start,len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        int i,brightness;
        for (brightness=startbrightness; (startbrightness > endbrightness ? brightness>=endbrightness:  brightness<=endbrightness) ;brightness+=step){
            for (i=start;i<start+len;i++){
                leds[i].brightness=brightness;
            }
            ws2811_render(&ledstring);
            usleep(delay * 1000);
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}


//makes some leds blink between 2 given colors for x times with a given delay
//blink <channel>,<color1>,<color2>,<delay>,<blink_count>,<startled>,<len>
void blink (char * args){
	int channel=0, color1=0, color2=0xFFFFFF,delay=1000, count=10;
	unsigned int start=0, len=0;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;;
	}	
	
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & color1, ledstring.channel[channel].color_size);
	if (is_valid_channel_number(channel)) args = read_color_arg(args, & color2, ledstring.channel[channel].color_size);
	args = read_int(args, & delay);
	args = read_int(args, & count);
	args = read_int(args, & start);
	args = read_int(args, & len);
	
            
	if (is_valid_channel_number(channel)){

        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        if (delay<=0) delay=100;
        
        if (debug) printf("blink %d, %d, %d, %d, %d, %d, %d\n", channel, color1, color2, delay, count, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        int i,blinks;
        for (blinks=0; blinks<count;blinks++){
            for (i=start;i<start+len;i++){
                if ((blinks%2)==0) {
					leds[i].color=color1;
				}else{
					leds[i].color=color2;
				}
            }
            ws2811_render(&ledstring);
            usleep(delay * 1000);
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//generates a brightness gradient pattern of a color component or brightness level
//gradient <channel>,<RGBWL>,<startlevel>,<endlevel>,<startled>,<len>
void gradient (char * args){
    char value[MAX_VAL_LEN];
	int channel=0, startlevel=0,endlevel=255;
	unsigned int start=0, len=0;
    char component='L'; //L is brightness level
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;;
	}	
	args = read_val(args, value, MAX_VAL_LEN);
	component=toupper(value[0]);
	args = read_int(args, & startlevel);
	args = read_int(args, & endlevel);
	args = read_int(args, & start);
	args = read_int(args, & len);

            
	if (is_valid_channel_number(channel)){
        if (startlevel>0xFF) startlevel=255;
        if (endlevel>0xFF) endlevel=255;
        
        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
        
        
        float step = 1.0*(endlevel-startlevel) / (float)(len-1);
        
        if (debug) printf("gradient %d, %c, %d, %d, %d,%d\n", channel, component, startlevel, endlevel, start,len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        
        float flevel = startlevel;
        int i;
        for (i=0; i<len;i++){
            unsigned int level = (unsigned int) flevel;
            if (i==len-1) level = endlevel;
            switch (component){
                case 'R':
                    leds[i+start].color = (leds[i+start].color & 0xFFFFFF00) | level;
                    break;
                case 'G':
                    leds[i+start].color = (leds[i+start].color & 0xFFFF00FF) | (level << 8);
                    break;
                case 'B':
                    leds[i+start].color = (leds[i+start].color & 0xFF00FFFF) | (level << 16);
                    break;
                case 'W':
                    leds[i+start].color = (leds[i+start].color & 0x00FFFFFF) | (level << 24);
                    break;
                case 'L':
                    leds[i+start].brightness=level;
                    break;
            }
            flevel+=step;
        } 
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//generates random colors
//random <channel>,<start>,<len>,<RGBWL>
void add_random(char * args){
    char value[MAX_VAL_LEN];
	int channel=0;
	unsigned int start=0, len=0;
    char component='L'; //L is brightness level
    int use_r=1, use_g=1, use_b=1, use_w=1, use_l=1;
    
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;;
	}	
	args = read_int(args, & start);
	args = read_int(args, & len);
	if (args!=NULL && *args!=0){
		args = read_val(args, value, MAX_VAL_LEN);
		use_r=0, use_g=0, use_b=0, use_w=0, use_l=0;
		unsigned char i;
		for (i=0;i<strlen(value);i++){
			switch(toupper(value[i])){
				case 'R':
					use_r=1;
					break;
				case 'G':
					use_g=1;
					break;
				case 'B':
					use_b=1;
					break;
				case 'W':
					use_w=1;
					break;
				case 'L':
					use_l=1;
					break;
			}
		}
	}	
    
    if (is_valid_channel_number(channel)){

        if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
     
        if (debug) printf("random %d,%d,%d\n", channel, start, len);
        
        ws2811_led_t * leds = ledstring.channel[channel].leds;
        //unsigned int colors = ledstring[channel].color_size;
        unsigned char r=0,g=0,b=0,w=0,l=0;
        unsigned int i;
        for (i=0; i<len;i++){
            if (use_r) r = rand() % 256;
            if (use_g) g = rand() % 256;
            if (use_b) b = rand() % 256;
            if (use_w) w = rand() % 256;
            if (use_l) l = rand() % 256;
            
            if (use_r || use_g || use_b || use_w) leds[start+i].color = color_rgbw(r,g,b,w);
            if (use_l) leds[start+i].brightness = l;
        }
    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

typedef struct {
	int led_index;
	int brightness;
	int delay; //random delay to delay effect
	int start_brightness;
	int start_color;
}fade_in_out_led_status;

//finds a random index which is currently not used by random_fade_in_out
int find_random_free_led_index(fade_in_out_led_status * led_status, unsigned int count, unsigned int start, unsigned int len){
	int i,j,k;
	int index=-1;
	int found = 1;
	k=0;
	//first try random
	while (found && k < (len * 2)){ //k to prevent endless loop
		index = start + (rand() % len);
		found = 0;
		for (j=0; j<count;j++) {
			if (led_status[j].led_index == index){
				found = 1;
				break;
			}
		}
		k++;
	}
	if (found){
		index = -1;
		//if count is too high just search for one index still available
		for (j=start;j<start+len;j++){
			found=0;
			for (i=0; i<count;i++) {
				if (led_status[i].led_index == j){
					found = 1;//found it
					break;
				}
			}
			if (found==0){ //didn't find, can use this index
				index=j;	
				break;
			}
		}
	}
	return index;
}

//creates some kind of random blinking leds effect
//random_fade_in_out <channel>,<duration Sec>,<count>,<delay>,<step>,<sync_delay>,<inc_dec>,<brightness>,<start>,<len>,<color>
//duration = total max duration of effect
//count = max number of leds that will fade in or out at same time
//delay = delay between changes in brightness
//step = ammount of brightness to increase between delays
//inc_dec = if 1 brightness will start at <brightness> and decrease to initial brightness of the led, else it will start low and go up
//start  = start at led position
//len  = stop at led position
//color  = use specific color, after blink effect color will return to initial
//brightness = max brightness of blinking led
void random_fade_in_out(char * args){
	unsigned int channel=0, start=0, len=0, count=0, duration=10, delay=1, step=20, sync_delay=0, inc_dec=1, brightness=255,color=0, change_color=0, i;
    fade_in_out_led_status *led_status;
	
    if (is_valid_channel_number(channel)){
        len = ledstring.channel[channel].count;;
    }
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;
		count = len / 3;
		args = read_int(args, & duration);
		args = read_int(args, & count);
		args = read_int(args, & delay);		
		args = read_int(args, & step);
		args = read_int(args, & sync_delay);
		args = read_int(args, & inc_dec);
		args = read_int(args, & brightness);
		args = read_int(args, & start);
		args = read_int(args, & len);
		change_color = args!=NULL && *args!=0;
		args = read_color_arg(args, & color, ledstring.channel[channel].color_size);
		args = read_brightness(args, & brightness);
		
		if (start>=ledstring.channel[channel].count) start=0;
        if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
		if (count>len) count = len;
		
		if (debug) printf("random_fade_in_out %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n", channel, count, delay, step, sync_delay, inc_dec, brightness, start, len, color);
		
		led_status = (fade_in_out_led_status *)malloc(count * sizeof(fade_in_out_led_status));
		ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		ws2811_render(&ledstring);
		
		for (i=0; i<count;i++){ //first assign count random leds for fading
			int index=find_random_free_led_index(led_status, count, start, len);
			led_status[i].led_index = index;
			if (index!=-1){ //assign
				led_status[i].delay = sync_delay ?  (rand() % sync_delay) : 0;
				led_status[i].start_brightness = leds[index].brightness;
				led_status[i].start_color = leds[index].color;
				led_status[i].led_index = index;
				led_status[i].brightness = brightness;
			}
		}
		
		unsigned int start_time = time(0);
		
		while (((time(0) - start_time) < duration) || duration==0){
			for (i=0;i<count; i++){
				if (led_status[i].delay<=0){
					if (led_status[i].led_index!=-1){
						leds[led_status[i].led_index].brightness = led_status[i].brightness;
						if (change_color) leds[led_status[i].led_index].color = color;
						if (inc_dec) led_status[i].brightness--;
						if ((inc_dec==1 && led_status[i].brightness <= led_status[i].start_brightness) || (inc_dec==0 && led_status[i].brightness >= led_status[i].start_brightness)){
							leds[led_status[i].led_index].brightness = led_status[i].start_brightness;
							if (change_color) leds[led_status[i].led_index].color = led_status[i].start_color;
							int index=find_random_free_led_index(led_status, count, start, len);
							if (index!=-1){	
								led_status[i].led_index = index;
								led_status[i].brightness = brightness;
								led_status[i].start_brightness = leds[led_status[i].led_index].brightness;
								led_status[i].start_color = leds[led_status[i].led_index].color;
								led_status[i].delay = sync_delay ?  (rand() % sync_delay) : 0;
							}
						}
					}
				}else{
					led_status[i].delay--;
				}
			}		
			ws2811_render(&ledstring);
			usleep(delay * 1000);				
		}
		
		for (i=0;i<count;i++){
			leds[led_status[i].led_index].brightness = led_status[i].start_brightness;
			if (change_color) leds[led_status[i].led_index].color = led_status[i].start_color;
		}
		ws2811_render(&ledstring);
		free (led_status);
	}else{
		fprintf(stderr, ERROR_INVALID_CHANNEL);
		
	}
	
}


//chaser makes leds run accross the led strip
//chaser <channel>,<duration>,<color>,<count>,<direction>,<delay>,<start>,<len>,<brightness>,<loops>
//channel = 1
//duration = time in seconds, or 0 4ever
//color = color to use
//count = number of leds
//direction = scroll direction
//delay = delay between moving the leds, speed
//start = start index led (default 0)
//len = length of the chaser (default enitre strip)
//brightness = brightness of the chasing leds
//loops = max number of chasing loops, 0 = 4ever, default = 0
void chaser(char * args){
	unsigned int channel=0, direction=1, duration=10, delay=10, color=255, brightness=255, loops=0;
	int i, n, index, len=0, count=1, start=0;
	
	args = read_channel(args, & channel);

	if (is_valid_channel_number(channel)){
		len = ledstring.channel[channel].count;
		args = read_int(args, & duration);
		args = read_color_arg(args, & color, ledstring.channel[channel].color_size);
		args = read_int(args, & count);
		args = read_int(args, & direction);
		args = read_int(args, & delay);
		args = read_int(args, & start);
		args = read_int(args, & len);
		args = read_brightness(args, & brightness);
		args = read_int(args, & loops);
	}
	
	if (is_valid_channel_number(channel)){
		if (start>=ledstring.channel[channel].count) start=0;
		if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
		if (count>len) count = len;
		
		if (debug) printf("chaser %d %d %d %d %d %d %d %d %d %d\n", channel, duration, color, count, direction, delay, start, len, brightness, loops);
	
		ws2811_led_t * org_leds = malloc(len * sizeof(ws2811_led_t));
		ws2811_led_t * leds = ledstring.channel[channel].leds;
		memcpy(org_leds, &leds[start], len * sizeof(ws2811_led_t)); //create a backup of original leds
		
		int loop_count=0;
		
		unsigned int start_time = time(0);
		while ((((time(0) - start_time) < duration) || duration==0) && (loops==0 || loops < loop_count)){
			ws2811_led_t tmp_led;
			
			for (n=0;n<count;n++){
				index = direction==1 ? i - n: len - i + n;
				if (loop_count>0 || (index > 0 && index < len)){
					index = (index + len) % len;
					leds[start + index].color = color;
					leds[start + index].brightness = brightness;	
				}
			}
			
			ws2811_render(&ledstring);
			usleep(delay * 1000);
			
			for (n=0;n<count;n++){
				index = direction==1 ? i - n : len - i + n;
				index = (index + len) % len;			
				leds[start + index].color = org_leds[index].color;
				leds[start + index].brightness = org_leds[index].brightness;	
			}
			
			i++;
			i = i % len;
			if (i==0){
				loop_count++;
			}
		}	
	
		memcpy(org_leds, & leds[start], len * sizeof(ws2811_led_t));
		free(org_leds);
	}else{
		fprintf(stderr, ERROR_INVALID_CHANNEL);
	}
}


//fills pixels with rainbow effect
//count tells how many rainbows you want
//color_change <channel>,<startcolor>,<stopcolor>,<duration>,<start>,<len>
//start and stop = color values on color wheel (0-255)
void color_change(char * args) {
	int channel=0, count=1,start=0,stop=255,startled=0, len=0, duration=10000, delay=10;
	
    if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & start);
	args = read_int(args, & stop);
	args = read_int(args, & duration);
	args = read_int(args, & startled);
	args = read_int(args, & len);
	
	if (is_valid_channel_number(channel)){
        if (start<0 || start > 255) start=0;
        if (stop<0 || stop > 255) stop = 255;
        if (startled<0) startled=0;
        if (startled+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-startled;
        
        if (debug) printf("color_change %d,%d,%d,%d,%d,%d\n", channel, start, stop, duration, startled, len);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		unsigned long long start_time = time_ms();
		unsigned long long curr_time = time_ms() - start_time;
		
		while (curr_time < duration){
			unsigned int color = deg2color(abs(stop-start) * curr_time / duration + start);
			
			for(i=0; i<numPixels; i++) {
				leds[startled+i].color = color;
			}			
			
			ws2811_render(&ledstring);
			usleep(delay * 1000);	
			curr_time = time_ms() - start_time;			
		}
		
		

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//fly in pixels from left or right filling entire string with a color
//fly_in <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<start_brightness>,<color>
//direction = 0/1 fly in from left or right default 1
//delay = delay in ms between moving pixel, default 10ms
//brightness = the final brightness of the leds that fly in
//start  = where to start effect default 0
//len = number of leds from start default length of strip
//start_brightness = initial brightness for all leds default is 0 (black)
//color = final color of the leds default is to use the current color
//first have to call "fill <channel>,<color>" to initialze a color if you leave color default value
void fly_in(char * args) {
	int channel=0,start=0, len=0, brightness=255, delay=10, direction=1, start_brightness=0, use_color=0;
	unsigned int color, tmp_color, repl_color;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & direction);
	args = read_int(args, & delay);
	args = read_int(args, & brightness);
	args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_int(args, & start_brightness);
	use_color = (args!=NULL && (*args)!=0);

	if (is_valid_channel_number(channel)){
		args = read_color_arg(args, & color, ledstring.channel[channel].color_size);		
        if (start<0) start=0;
        if (start+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-start;
        
        if (debug) printf("fly_in %d,%d,%d,%d,%d,%d,%d,%d,%d\n", channel, direction, delay, brightness, start, len, start_brightness, color, use_color);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		for (i=0;i<len;i++){
			leds[start+i].brightness=start_brightness;
		}
		
		ws2811_render(&ledstring);
		for (i=0;i<len;i++){
			if (use_color){
				repl_color = color;
			}else{
				if (direction){
					repl_color = leds[start+len-i-1].color;
				}else{
					repl_color = leds[start+i].color;
				}
			}
			for (j=0;j<len - i;j++){
				if (direction){
					leds[start+j].brightness = brightness;
					tmp_color = leds[start+j].color;
					leds[start+j].color = repl_color;
				}else{
					leds[start+len-j-1].brightness = brightness;
					tmp_color = leds[start+len-j-1].color;
					leds[start+len-j-1].color = repl_color;
				}
				ws2811_render(&ledstring);
				usleep(delay * 1000);
				if (direction){
					leds[start+j].brightness = start_brightness;	
					leds[start+j].color = tmp_color;
				}else{
					leds[start+len-j-1].brightness = start_brightness;
					leds[start+len-j-1].color = tmp_color;
				}
			}
			if (direction){
				leds[start+len-i-1].brightness = brightness;
				leds[start+len-i-1].color = repl_color;
			}else{
				leds[start+i].brightness = brightness;
				leds[start+i].color = repl_color;				
			}
			ws2811_render(&ledstring);
			usleep(delay * 1000);		
		}

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}

//fly out pixels from left or right filling entire string with black or a color/brightness
//fly_out <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<end_brightness>,<color>
//direction = 0/1 fly out from left or right default 1
//delay = delay in ms between moving pixel, default 10ms
//brightness = the final brightness of the leds that fly in
//start  = where to start effect default 0
//len = number of leds from start default length of strip
//end_brightness = brightness for all leds at the end, default is 0 = black
//color = final color of the leds default is to use the current color
//first have to call "fill <channel>,<color>" to initialze a color in each led before start fly_out
void fly_out(char * args) {
	int channel=0,start=0, len=0, delay=10, direction=1, brightness=255, use_color=0, end_brightness=0;
	unsigned int color, tmp_color, repl_color;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_int(args, & direction);
	args = read_int(args, & delay);
	args = read_int(args, & brightness);
	args = read_int(args, & start);
	args = read_int(args, & len);
	args = read_int(args, & end_brightness);
	use_color = (args!=NULL && (*args)!=0);
	
	if (is_valid_channel_number(channel)){		
		args = read_color_arg(args, & color, ledstring.channel[channel].color_size);
        if (start<0) start=0;
        if (start+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-start;
        
        if (debug) printf("fly_out %d,%d,%d,%d,%d,%d,%d,%d,%d\n", channel, direction, delay, brightness, start, len, end_brightness, color, use_color);
        
        int numPixels = len; //ledstring.channel[channel].count;;
        int i, j;
        ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		ws2811_render(&ledstring);
		for (i=0;i<len;i++){
			if (direction){
				repl_color = leds[start+i].color;
			}else{
				repl_color = leds[start+len-i-1].color;
			}			
			if (direction){				
				leds[start+i].brightness = end_brightness;
				if (use_color) leds[start+i].color = color;
			}else{
				leds[start+len-i-1].brightness = end_brightness;
				if (use_color) leds[start+len-i-1].color = color;				
			}
			
			for (j=0;j<=i;j++){
				if (direction){
					leds[start+i-j].brightness = brightness;
					tmp_color = leds[start+i-j].color;
					leds[start+i-j].color = repl_color;
				}else{
					leds[start+len-i-1+j].brightness = brightness;
					tmp_color = leds[start+len-i-1+j].color;
					leds[start+len-i-1+j].color = repl_color;
				}
				ws2811_render(&ledstring);
				usleep(delay * 1000);
				if (direction){
					leds[start+i-j].brightness = end_brightness;	
					leds[start+i-j].color = tmp_color;
				}else{
					leds[start+len-i-1+j].brightness = end_brightness;
					leds[start+len-i-1+j].color = tmp_color;
				}
			}

			ws2811_render(&ledstring);
			usleep(delay * 1000);		
		}

    }else{
        fprintf(stderr,ERROR_INVALID_CHANNEL);
    }
}


void start_loop (char * args){
    if (mode==MODE_FILE){
        if (loop_index>=MAX_LOOPS){
            loop_index=MAX_LOOPS-1;
            printf("Warning max nested loops reached!\n");
            return;
        }
        if (debug) printf ("do %d\n", ftell(input_file));
        loops[loop_index].do_pos = ftell(input_file);
        loops[loop_index].n_loops=0;
        loop_index++;
    }else if (mode==MODE_TCP){
        if (loop_index<MAX_LOOPS){
            if (debug) printf ("do %d\n", thread_read_index);
            loops[loop_index].do_pos = thread_read_index;
            loops[loop_index].n_loops=0;
            loop_index++;
        }else{
            printf("Warning max nested loops reached!\n");
        }
    }
}

void end_loop(char * args){
    int max_loops = 0; //number of wanted loops
	int step = 1;
    if (args!=NULL){
		args = read_int(args, &max_loops);
		args = read_int(args, &step);
    }
    if (mode==MODE_FILE){
        if (debug) printf ("loop %d, %d, %d\n", ftell(input_file), max_loops, step);
        if (loop_index==0){ //no do found!
            fseek(input_file, 0, SEEK_SET);
        }else{
            loops[loop_index-1].n_loops+=step;
            if (max_loops==0 || loops[loop_index-1].n_loops<max_loops){ //if number of loops is 0 = loop forever
                fseek(input_file, loops[loop_index-1].do_pos,SEEK_SET);
            }else{
                if (loop_index>0) loop_index--; //exit loop
            }
        }
    }else if (mode==MODE_TCP){
        if (debug) printf("loop %d\n", thread_read_index);
        if (loop_index==0){
            thread_read_index=0; 
        }else{
            loops[loop_index-1].n_loops+=step;
            if (max_loops==0 || loops[loop_index-1].n_loops<max_loops){ //if number of loops is 0 = loop forever
                thread_read_index = loops[loop_index-1].do_pos;
            }else{
                if (loop_index>0) loop_index--; //exit loop
            }       
        }
    }
}



//read JPEG image and put pixel data to LEDS
//readjpg <channel>,<FILE>,<start>,<len>,<offset>,<OR AND XOR NOT =>,<delay>
//offset = where to start in JPEG file
//DELAY = delay ms between 2 reads of LEN pixels, default=0 if 0 only <len> bytes at <offset> will be read
#ifdef USE_JPEG
void readjpg(char * args){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=0, offset=0;
	int op=0,delay=0;
    
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_str(args, filename, sizeof(filename));
	args = read_int(args, &start);
	args = read_int(args, &len);
	args = read_int(args, &offset);
	args = read_str(args, value, sizeof(value));
	if (strcmp(value, "OR")==0) op=1;
	else if (strcmp(value, "AND")==0) op=2;
	else if (strcmp(value, "XOR")==0) op=3;
	else if (strcmp(value, "NOT")==0) op=4;
	args = read_int(args, &delay);
	
    
    if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		int row_stride;		/* physical row width in output buffer */
		
		
		if (debug) printf("readjpg %d,%s,%d,%d,%d,%d,%d\n", channel, filename, start, len, offset, op, delay);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}

		// We set up the normal JPEG error routines, then override error_exit.
		cinfo.err = jpeg_std_error(&jerr.pub);
		jerr.pub.error_exit = my_error_exit;
		// Establish the setjmp return context for my_error_exit to use.
		if (setjmp(jerr.setjmp_buffer)) {
			/* If we get here, the JPEG code has signaled an error.
			 * We need to clean up the JPEG object, close the input file, and return.
			 */
			jpeg_destroy_decompress(&cinfo);
			fclose(infile);
			return;
		}
		
		// Now we can initialize the JPEG decompression object.
		jpeg_create_decompress(&cinfo);
		jpeg_stdio_src(&cinfo, infile);

		jpeg_read_header(&cinfo, TRUE);
		jpeg_start_decompress(&cinfo);

		row_stride = cinfo.output_width * cinfo.output_components;

		JSAMPARRAY buffer;	// Output row buffer
		int i=0,jpg_idx=0,led_idx; //pixel index for current row, jpeg image, led string
		buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

		ws2811_led_t * leds = ledstring.channel[channel].leds;
		
		if (start>=ledstring.channel[channel].count) start=0;
		if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
		
		led_idx=start; //start at this led index
		
		int eofstring=0;
		while (eofstring==0 && cinfo.output_scanline < cinfo.output_height) {
			jpeg_read_scanlines(&cinfo, buffer, 1);
			for(i=0;i<cinfo.image_width;i++){
				if (jpg_idx>=offset){ //check jpeg offset
					unsigned char r,g,b;
					r = buffer[0][i*cinfo.output_components];
					g = buffer[0][i*cinfo.output_components+1];
					b = buffer[0][i*cinfo.output_components+2];
					if (cinfo.output_components==1){ //grayscale image
						g = r;
						b = r;
					}
					if (led_idx<len){
						if (debug) printf("led %d= r %d,g %d,b %d, jpg idx=%d\n", led_idx, r, g, b,jpg_idx);
						int fill_color = color(r,g,b);
						switch (op){
							case 0:
								leds[led_idx].color=fill_color;
								break;
							case 1:
								leds[i].color|=fill_color;
								break;
							case 2:
								leds[i].color&=fill_color;
								break;
							case 3:
								leds[i].color^=fill_color;
								break;
							case 4:
								leds[i].color=~fill_color;
								break;
						}
					}
					led_idx++;
					if ( led_idx==len){ 
						if (delay!=0){//reset led index if we are at end of led string and delay
							led_idx=0;
							ws2811_render(&ledstring);
							usleep(delay * 1000);
						}else{
							eofstring=1;
							break;
						}
					}
				}
				jpg_idx++;
			}
		}

		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
	}
}
#endif

//read PNG image and put pixel data to LEDS
//readjpg <channel>,<FILE>,<BACKCOLOR>,<start>,<len>,<offset>,<OR AND XOR =>,<DELAY>
//offset = where to start in PNG file
//backcolor = color to use for transparent area, FF0000 = RED
//P = use the PNG backcolor (default)
//W = use the alpha data for the White leds in RGBW LED strips
//DELAY = delay ms between 2 reads of LEN pixels, default=0 if 0 only <len> bytes at <offset> will be read
#ifdef USE_PNG
void readpng(char * args){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=0, offset=0;
	int op=0;
	int backcolor=0;
    int backcolortype=0; //0 = use PNG backcolor, 1 = use given backcolor, 2 = no backcolor but use alpha for white leds
	int delay=0;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_str(args, filename, sizeof(filename));
	args = read_str(args, value, sizeof(filename));
	if (strlen(value)>=6){
		if (is_valid_channel_number(channel)){
			read_color(value, & backcolor, ledstring.channel[channel].color_size);
			backcolortype=1;
		}
	}else if (strcmp(value, "W")==0){
		backcolortype=2;
	}	
	args = read_int(args, &start);
	args = read_int(args, &len);
	args = read_int(args, &offset);
	args = read_str(args, value, sizeof(value));
	if (strcmp(value, "OR")==0) op=1;
	else if (strcmp(value, "AND")==0) op=2;
	else if (strcmp(value, "XOR")==0) op=3;
	else if (strcmp(value, "NOT")==0) op=4;
	args = read_int(args, &delay);
	
	if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		ulg image_width, image_height, image_rowbytes;
		int image_channels,rc;
		uch *image_data;
		uch bg_red=0, bg_green=0, bg_blue=0;

		if (start<0) start=0;
        if (start+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-start;
		
		if (debug) printf("readpng %d,%s,%d,%d,%d,%d,%d,%d\n", channel, filename, backcolor, start, len,offset,op, delay);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}
		
		if ((rc = readpng_init(infile, &image_width, &image_height)) != 0) {
            switch (rc) {
                case 1:
                    fprintf(stderr, "[%s] is not a PNG file: incorrect signature.\n", filename);
                    break;
                case 2:
                    fprintf(stderr, "[%s] has bad IHDR (libpng longjmp).\n", filename);
                    break;
                case 4:
                    fprintf(stderr, "Read PNG insufficient memory.\n");
                    break;
                default:
                    fprintf(stderr, "Unknown readpng_init() error.\n");
                    break;
            }
            fclose(infile);
			return;
        }
		
		//get the background color (for transparency support)
		if (backcolortype==0){
			if (readpng_get_bgcolor(&bg_red, &bg_green, &bg_blue) > 1){
				readpng_cleanup(TRUE);
				fclose(infile);
				fprintf(stderr, "libpng error while checking for background color\n");
				return;
			}
		}else{
			bg_red = get_red(backcolor);
			bg_green = get_green(backcolor);
			bg_blue = get_blue(backcolor);
		}
		
		//read entire image data
		image_data = readpng_get_image(2.2, &image_channels, &image_rowbytes);
		
		if (image_data) {
			int row=0, led_idx=0, png_idx=0, i=0;
			uch r, g, b, a;
			uch *src;
			
			ws2811_led_t * leds = ledstring.channel[channel].leds;
		
			if (start>=ledstring.channel[channel].count) start=0;
			if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
			
			led_idx=start; //start at this led index
			//load all pixels
			for (row = 0;  row < image_height; row++) {
				src = image_data + row * image_rowbytes;
				
				for (i = image_width;  i > 0;  --i) {
					r = *src++;
					g = *src++;
					b = *src++;
					
					if (image_channels != 3){
						a = *src++;
						if (backcolortype!=2){
							r = alpha_component(r, bg_red,a);
							g = alpha_component(g, bg_green,a);
							b = alpha_component(b, bg_blue,a);
						}
					}					
					if (png_idx>=offset){
						if (debug) printf("led %d= r %d,g %d,b %d,a %d, PNG channels=%d, PNG idx=%d\n", led_idx, r, g, b, a,image_channels,png_idx);
						if (led_idx<len){
							int fill_color;
							if (backcolortype==2 && ledstring.channel[channel].color_size>3){
								fill_color=color_rgbw(r,g,b,a);
							}else{
								fill_color=color(r,g,b);
							}
		
							switch (op){
								case 0:
									leds[led_idx].color=fill_color;
									break;
								case 1:
									leds[i].color|=fill_color;
									break;
								case 2:
									leds[i].color&=fill_color;
									break;
								case 3:
									leds[i].color^=fill_color;
									break;
								case 4:
									leds[i].color=~fill_color;
									break;
							}
							
						}
						led_idx++;
						if ( led_idx==len){ 
							if (delay!=0){//reset led index if we are at end of led string and delay
								led_idx=0;
								ws2811_render(&ledstring);
								usleep(delay * 1000);
							}else{
								row = image_height; //exit reading
								i=0;
								break;
							}
						}
					}
					png_idx++;
				}
			}
			readpng_cleanup(TRUE);
		}else{
			readpng_cleanup(FALSE);
			fprintf(stderr, "Unable to decode PNG image\n");
		}
		fclose(infile);
    }
}
#endif

//initializes the memory for a TCP/IP multithread buffer
void init_thread(char * data){
    if (thread_data==NULL){
        thread_data = (char *) malloc(DEFAULT_BUFFER_SIZE);
        thread_data_size = DEFAULT_BUFFER_SIZE;
    }                
    start_thread=0;
    thread_read_index=0;
    thread_write_index=0;
    thread_running=0;
    write_to_thread_buffer=1; //from now we save all commands to the thread buffer
}

//expands the TCP/IP multithread buffer (increase size by 2)
void expand_thread_data_buffer(){
    thread_data_size = thread_data_size * 2;
    char * tmp_buffer = (char *) malloc(thread_data_size);
    memcpy((void*) tmp_buffer, (void*)thread_data, thread_data_size);
    free(thread_data);
    thread_data = tmp_buffer;
}

//adds data to the thread buffer 
void write_thread_buffer (char c){
    thread_data[thread_write_index] = c;
    thread_write_index++;
    if (thread_write_index==thread_data_size) expand_thread_data_buffer();
}

//this function can be run in other thread for TCP/IP to enable do ... loops  (usefull for websites)
void thread_func (void * param){
    thread_read_index=0;
    if (debug) printf("Enter thread %d,%d,%d.\n", thread_running,thread_read_index,thread_write_index);
    while (thread_running){
        char c = thread_data[thread_read_index];
        //if(debug) printf("Process char %c %d\n", c, thread_read_index);
        process_character(c);
        thread_read_index++;
        if (thread_read_index>=thread_write_index) break; //exit loop if we are at the end of the file
    }
    thread_running=0;
    if (debug) printf("Exit thread.\n");
    pthread_exit(NULL); //exit the tread
}

void str_replace(char * dst, char * src, char * find, char * replace){
	char *p;
	size_t replace_len = strlen(replace);
	size_t find_len = strlen(find);
	
	p = strstr(src, find);
	while (p){
		strncpy(dst, src, p - src); //copy first part
		dst+=p-src;
		strcpy(dst, replace);
		dst+=replace_len;
		p+=find_len;
		src=p;
		p = strstr(p, find);
	}
	if (*src!='\0') strcpy(dst, src); //copy last part
}

//executes 1 command line
void execute_command(char * command_line){
    
    if (command_line[0]=='#') return; //=comments
    
    if (write_to_thread_buffer){
        if (strncmp(command_line, "thread_stop", 11)==0){
            if (mode==MODE_TCP){
                write_to_thread_buffer=0;
                if (debug) printf("Thread stop.\n");
                if (thread_write_index>0) start_thread=1; //remember to start the thread when client closes the TCP/IP connection
            }        
        }else{
            if (debug) printf("Write to thread buffer: %s\n", command_line);
            while (*command_line!=0){
                write_thread_buffer(*command_line); //for TCP/IP we write to the thread buffer
                command_line++;
            }
            write_thread_buffer(';');
        }
    }else{
		char * raw_args = strchr(command_line, ' ');		
        char * command =  strtok(command_line, " \r\n");

		char * arg = NULL;
		
		if (raw_args!=NULL){
			raw_args++;			
			if (strlen(raw_args)>0){
				arg = (char*) malloc(strlen(raw_args)*2);
				char * tmp_arg = (char *) malloc(strlen(raw_args)*2);
				int i=0;
				char find_loop_nr[MAX_LOOPS+2];
				char replace_loop_index[MAX_LOOPS+2];
				strcpy(arg,raw_args);
				for (i=0;i<loop_index;i++){
					sprintf(find_loop_nr, "{%d}", i);
					sprintf(replace_loop_index, "%d", loops[i].n_loops);
					str_replace(tmp_arg, arg, find_loop_nr, replace_loop_index); //cannot put result in same string we are replacing, store in temp buffer
					strcpy(arg, tmp_arg);
				}
				free(tmp_arg);
			}
		}
        
        if (strcmp(command, "render")==0){
            render(arg);
        }else if (strcmp(command, "rotate")==0){
            rotate(arg);
        }else if (strcmp(command, "delay")==0){
            if (arg!=NULL)	usleep((atoi(arg)+1)*1000);
        }else if (strcmp(command, "brightness")==0){
            brightness(arg);
        }else if (strcmp(command, "rainbow")==0){
            rainbow(arg);
        }else if (strcmp(command, "fill")==0){	
            fill(arg);
        }else if (strcmp(command, "fade")==0){
            fade(arg);
        }else if (strcmp(command, "gradient")==0){
            gradient(arg);
        }else if (strcmp(command, "random")==0){
            add_random(arg);
        }else if (strcmp(command, "do")==0){
            start_loop(arg);
        }else if (strcmp(command, "loop")==0){
            end_loop(arg);
        }else if (strcmp(command, "thread_start")==0){ //start a new thread that processes code
            if (thread_running==0 && mode==MODE_TCP) init_thread(arg);
        }else if (strcmp(command, "init")==0){ //first init ammount of channels wanted
            init_channels(arg);
        }else if (strcmp(command, "setup")==0){ //setup the channels
            setup_ledstring(arg);
        }else if (strcmp(command, "settings")==0){
            print_settings();
        }else if (strcmp(command, "global_brightness")==0){
            global_brightness(arg);
		}else if (strcmp(command, "blink")==0){
			blink(arg);
		}else if (strcmp(command, "random_fade_in_out")==0){
			random_fade_in_out(arg);
		}else if (strcmp(command, "chaser")==0){
			chaser(arg);
		}else if (strcmp(command, "color_change")==0){
			color_change(arg);
		}else if (strcmp(command, "fly_in")==0){
			fly_in(arg);
		}else if (strcmp(command, "fly_out")==0){
			fly_out(arg);
		#ifdef USE_JPEG
		}else if (strcmp(command, "readjpg")==0){
			readjpg(arg);
		#endif
		#ifdef USE_PNG
		}else if (strcmp(command, "readpng")==0){
			readpng(arg);
		#endif
        }else if (strcmp(command, "help")==0){
            printf("debug (enables some debug output)\n");
            printf("setup <channel>, <led_count>, <led_type>, <invert>, <global_brightness>, <gpionum>\n");
            printf("    led types:\n");
            printf("     0 WS2811_STRIP_RGB\n");
            printf("     1  WS2811_STRIP_RBG\n");
            printf("     2  WS2811_STRIP_GRB\n"); 
            printf("     3  WS2811_STRIP_GBR\n"); 
            printf("     4  WS2811_STRIP_BRG\n");
            printf("     5  WS2811_STRIP_BGR,\n"); 
            printf("     6  SK6812_STRIP_RGBW\n");
            printf("     7  SK6812_STRIP_RBGW\n");
            printf("     8  SK6812_STRIP_GRBW\n"); 
            printf("     9  SK6812_STRIP_GBRW\n");
            printf("     10 SK6812_STRIP_BRGW\n");
            printf("     11 SK6812_STRIP_BGRW\n");
            printf("init <frequency>,<DMA> (initializes PWM output, call after all setup commands)\n");
            printf("render <channel>,<start>,<RRGGBBWWRRGGBBWW>\n");
            printf("rotate <channel>,<places>,<direction>,<new_color>,<new_brightness>\n");
            printf("rainbow <channel>,<count>,<start_color>,<stop_color>,<start_led>,<len>\n");
            printf("fill <channel>,<color>,<start>,<len>,<OR,AND,XOR,NOT,=>\n");
            printf("brightness <channel>,<brightness>,<start>,<len> (brightness: 0-255)\n");
            printf("fade <channel>,<start_brightness>,<end_brightness>,<delay ms>,<step>,<start_led>,<len>\n");
            printf("gradient <channel>,<RGBWL>,<start_level>,<end_level>,<start_led>,<len>\n");
            printf("random <channel>,<start>,<len>,<RGBWL>\n");
			printf("random_fade_in_out <channel>,<duration Sec>,<count>,<delay>,<step>,<sync_delay>,<inc_dec>,<brightness>,<start>,<len>,<color>");
			printf("chaser <channel>,<duration>,<color>,<count>,<direction>,<delay>,<start>,<len>,<brightness>,<loops>\n");
			printf("color_change <channel>,<startcolor>,<stopcolor>,<duration>,<start>,<len>\n");
			printf("fly_in <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<start_brightness>,<color>\n");
			printf("fly_out <channel>,<direction>,<delay>,<brightness>,<start>,<len>,<end_brightness>,<color>\n");
			#ifdef USE_JPEG
			printf("readjpg <channel>,<file>,<LED start>,<len>,<JPEG Pixel offset>,<OR,AND,XOR,NOT,=>\n");
			#endif
			#ifdef USE_PNG
			printf("readpng <channel>,<file>,<BACKCOLOR>,<LED start>,<len>,<PNG Pixel offset>,<OR,AND,XOR,NOT,=>\n     BACKCOLOR=XXXXXX for color, PNG=USE PNG Back color (default), W=Use alpha for white leds in RGBW strips.\n");
			#endif
            printf("settings\n");
            printf("do ... loop (TCP / File mode only)\n");
			printf("Inside a finite loop {x} will be replaced by the current loop index number. x stands for the loop number in case of multiple nested loops (default use 0).");
            printf("exit\n");
        }else if (strcmp(command, "debug")==0){
            if (debug) debug=0;
            else debug=1;
        }else if (strcmp(command, "exit")==0){
            printf("Exiting.\n");
            exit_program=1;
        }else{
            printf("Unknown cmd: %s\n", command_line);
        }
		//free(arg);
    }
}

void process_character(char c){
    if (c=='\n' || c == '\r' || c == ';'){
        if (command_index>0){
            command_line[command_index]=0; //terminate with 0
            execute_command(command_line);
            command_index=0;
        }
    }else{
        if (!(command_index==0 && c==' ')){
            command_line[command_index]=(char)c;
            command_index++;
            if (command_index==command_line_size) command_index=0;
        }
    }
}

//for information see:
//http://www.linuxhowtos.org/C_C++/socket.htm
//waits for client to connect
void tcp_wait_connection (){
    socklen_t clilen;
    int sock_opt = 1;
    socklen_t optlen = sizeof (sock_opt);
    
    if (start_thread){
        if (debug) printf("Running thread.\n");
        thread_running=1; //thread will run untill thread_running becomes 0 (this is after a new client has connected)
        pthread_create(& thread, NULL, (void* (*)(void*)) & thread_func, NULL);
    }    
    
    printf("Waiting for client to connect.\n");
    
    clilen = sizeof(cli_addr);
    active_socket = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    
    if (setsockopt(active_socket, SOL_SOCKET, SO_KEEPALIVE, &sock_opt, optlen)) printf("Error set SO_KEEPALIVE\n");
    
    if (thread_running){//if there is a thread active we exit it 
        thread_running=0;
        pthread_join(thread,NULL); //wait for thread to finish and exit
    }
    
    write_to_thread_buffer=0;
    thread_write_index=0;
    thread_read_index=0;
    start_thread=0;
     
    printf("Client connected.\n");
}

//sets up sockets
void start_tcpip(int port){
	
     sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
     if (sockfd < 0) {
        fprintf(stderr,"ERROR opening socket\n");
        exit(1);
     }

     bzero((char *) &serv_addr, sizeof(serv_addr));

     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(port);
     if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr,"ERROR on binding.\n");
        exit(1);
     }
	 
	 printf("Listening on %d.\n", port);
     listen(sockfd,5);
     tcp_wait_connection();
}

void load_config_file(char * filename){
	FILE * file = fopen(filename, "r");
	
	if (debug) printf("Reading config file %s\n", filename);
	
	char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
		char * val = strchr(line, '=');
		char * cfg =  strtok(line, " =\t\r\n");
		
		if (val!=NULL) val++;
		if (debug) printf("Reading Config line %s, cfg=%s, val=%s\n", line, cfg, val);
		
		if (val!=NULL) val = strtok(val, "\r\n");
		while (val!=NULL && val[0]!=0 && (val[0]==' ' || val[0]=='\t')) val++;
		
		
		if (strcmp(cfg, "mode")==0 && val!=NULL){
			if (debug) printf("Setting mode %s\n", val);
			if (strcmp(val, "tcp")==0){
				mode = MODE_TCP;
			}else if (strcmp(val, "file")==0){
				mode = MODE_FILE;
			}else if (strcmp(val, "pipe")==0){
				mode = MODE_NAMED_PIPE;
			}else{
				fprintf(stderr, "Unknown mode %s\n", val);
			}
		}else if (strcmp(cfg, "file")==0 && val!=NULL){ 
			if (mode==MODE_FILE){
				if (debug) printf("Setting input file %s\n", val);
				input_file = fopen(val, "r");
			}
		}else if (strcmp(cfg, "port")==0 && val!=NULL){
			if (mode==MODE_TCP){
				int port = atoi(val);
				if (port==0) port=9999;
				if (debug) printf("Using TCP port %d\n", port);
			}
		}else if (strcmp(cfg, "pipe")==0 && val!=NULL){
			if (mode==MODE_NAMED_PIPE){
				if (debug) printf("Opening named pipe %s\n", val);
				named_pipe_file = (char*)malloc(strlen(val)+1);
				strcpy(named_pipe_file, val);
				remove(named_pipe_file);
				mkfifo(named_pipe_file,0777);
				chmod(named_pipe_file,0777);
				input_file = fopen(named_pipe_file, "r");
			}
		}else if (strcmp(cfg, "init")==0 && val!=NULL){
			if (strlen(val)>0){
				if (debug) printf("Initialize cmd: %s\n", val);
				initialize_cmd = (char*)malloc(strlen(val)+1);			
				strcpy(initialize_cmd, val);
			}
		}
        
    }

    fclose(file);
}

//main routine
int main(int argc, char *argv[]){
    int ret = 0;
	int i;
	int index=0;
    
    srand (time(NULL));

    ledstring.device=NULL;
    for (i=0;i<RPI_PWM_CHANNELS;i++){
        ledstring.channel[0].gpionum = 0;
        ledstring.channel[0].count = 0;
        ledstring.channel[1].gpionum = 0;
        ledstring.channel[1].count = 0;
    }
    
	command_line = NULL;
    named_pipe_file=NULL;
	malloc_command_line(DEFAULT_COMMAND_LINE_SIZE);

    setup_handlers();

    input_file = stdin; //by default we read from console, stdin
    mode = MODE_STDIN;
    
	int arg_idx=1;
	int port=0;
	while (argc>arg_idx){
        if (strcmp(argv[arg_idx], "-p")==0){ //use a named pipe, creates a file (by default in /dev/ws281x) which you can write commands to: echo "command..." > /dev/ws281x
            if (argc>arg_idx+1){
                named_pipe_file = (char*)malloc(strlen(argv[arg_idx+1])+1);
                strcpy(named_pipe_file,argv[arg_idx+1]);
				arg_idx++;
            }else{
                named_pipe_file = (char*)malloc(strlen(DEFAULT_DEVICE_FILE)+1);
                strcpy(named_pipe_file, DEFAULT_DEVICE_FILE);
            }
            printf ("Opening %s as named pipe.\n", named_pipe_file);
            remove(named_pipe_file);
            mkfifo(named_pipe_file,0777);
            chmod(named_pipe_file,0777);
            input_file = fopen(named_pipe_file, "r");
            mode  = MODE_NAMED_PIPE;
        }else if (strcmp(argv[arg_idx], "-f")==0){ //read commands / data from text file
            if (argc>arg_idx+1){
                input_file = fopen(argv[arg_idx+1], "r");
                printf("Opening %s.\n", argv[arg_idx+1]);
				arg_idx++;
            }else{
                fprintf(stderr,"Error you must enter a file name after -f option\n");
                exit(1);
            }
            mode = MODE_FILE;
        }else if (strcmp(argv[arg_idx], "-tcp")==0){ //open up tcp ip port and read commands from there
            if (argc>arg_idx+1){
                port = atoi(argv[arg_idx+1]);
                if (port==0) port=9999;
				arg_idx++;
				mode = MODE_TCP;
            }else{
                fprintf(stderr,"You must enter a port after -tcp option\n");
                exit(1);
            }
		}else if (strcmp(argv[arg_idx], "-c")==0){ //load configuration file
			if (argc>arg_idx+1){
				load_config_file(argv[arg_idx+1]);
			}else{
				fprintf(stderr,"No configuration file given!\n");
				exit(1);
			}
        }else if (strcmp(argv[arg_idx], "-d")==0){ //turn debug on
			debug=1;
		}else if (strcmp(argv[arg_idx], "-i")==0){ //initialize command
			if (argc>arg_idx+1){
				arg_idx++;
				initialize_cmd = (char*)malloc(strlen(argv[arg_idx])+1);
				strcpy(initialize_cmd, argv[arg_idx]);
			}
		}else if (strcmp(argv[arg_idx], "-?")==0){
			printf("WS2812 Server program for Raspberry Pi V2.4");
			printf("Command line options:\n");
			printf("-p <pipename>       	creates a named pipe at location <pipename> where you can write command to.\n");
			printf("-f <filename>       	read commands from <filename>\n");
			printf("-tcp <port>         	listen for TCP connection to receive commands from.\n");
			printf("-d                  	turn debug output on.\n");
			printf("-i \"<commands>\"       initialize with <commands> (seperate and end with a ;)\n");
			printf("-c <filename>		    initializes using a configuration file (for running as deamon)\n");
			printf("-?                  	show this message.\n");
			return 0;
		}
		arg_idx++;
	}
	
	if ((mode == MODE_FILE || mode == MODE_NAMED_PIPE) && input_file==NULL){
		fprintf(stderr,"Error opening file!\n");
		exit(1);
	}
	
    int c;
	
	if (initialize_cmd!=NULL){
		for(i=0;i<strlen(initialize_cmd);i++){
			process_character(initialize_cmd[i]);
		}
		free(initialize_cmd);
		initialize_cmd=NULL;
	}
	
	if (mode==MODE_TCP) start_tcpip(port);
	
	while (exit_program==0) {
        if (mode==MODE_TCP){
            c = 0;
            if (read(active_socket, (void *) & c, 1)<=0) c = EOF; //returns 0 if connection is closed, -1 if no more data available and >0 if data read
        }else{
            c = fgetc (input_file); //doesn't work with tcp
        }
        
	  if (c!=EOF){
        process_character(c);
	  }else{
        //end of file or read error
		switch (mode){
            case MODE_TCP:
                if (!exit_program){
                    tcp_wait_connection(); //go back to wait for connection
                }
                break;
            case MODE_NAMED_PIPE:
				input_file = fopen(named_pipe_file, "r");
				//remove(named_pipe_file);
                //mkfifo(named_pipe_file, 0777);
                //chmod(named_pipe_file, 0777);
				break;
            case MODE_STDIN:
                usleep(10000);
                break;
            case MODE_FILE:
                process_character('\n'); //end last line
				exit_program=1; 
                //if (ftell(input_file)==feof(input_file))  exit_program=1; //exit the program if we reached the end
                break;
        }
	  }
    }
	
    if (mode==MODE_TCP){
        shutdown(active_socket,SHUT_RDWR);
        shutdown(sockfd,SHUT_RDWR);
        close(active_socket);
        close(sockfd);
    }else{
        fclose(input_file);
    }
        
    if (named_pipe_file!=NULL){
        remove(named_pipe_file);
        free(named_pipe_file);
    }
	free(command_line);
    if (thread_data!=NULL) free(thread_data);
    if (ledstring.device!=NULL) ws2811_fini(&ledstring);
    
    return ret;
}