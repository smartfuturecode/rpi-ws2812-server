#include <stdint.h>
#include <stdio.h>
#include "additionalfunctions.h"

#define MAX_VAL_LEN 255


#ifdef USE_WIRINGPI

//switches the state of a certain gpio
//gpio <pin>,<state>
void gpio(char * args){
	char value[MAX_VAL_LEN];
    char op=0;
	int gpio=0,state=0,len=-1;
	unsigned int fill_color=0;

	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		gpio = atoi(value);
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			state = atoi(value);
		}
	}

	if (state==0 || state ==1/*valid gpio */){
		state = (state==0?1:0);
           	if (debug) printf("gpio %d,%d\n", gpio, state);

        	digitalWrite(gpio,state);

    }else{
        fprintf(stderr,"Invalid gpio or state\n");
    }
}

//initializes wiringPi
void init_wiringPi(){

    if (debug) printf("Init wiringPi\n");
    ws2811_return_t ret;
    if (wiringPiSetup() == -1){
        fprintf(stderr, "wiringPiSetup failed\n");
    }
}
#endif

char * read_section(char * args,int num, char * start, char * end){
	int count=0;
    if (*args==':') args++;
	while(count<num && *args!=0 && *args!=',') {
		if (*args==':') count++;
		args++;
	}
	while (*args!=0 && *args!='-' && *args!=':' && *args!=','){
		if (*args!=' ' && *args!='\t'){ //skip space
			*start=*args;
			start++;
		}
		args++;
	}
	if(*args=='-'){
		args++;
		while (*args!=0 && *args!='-' && *args!=':' && *args!=','){
			if (*args!=' ' && *args!='\t'){ //skip space
				*end=*args;
				end++;
			}
			args++;
		}
	}
	*start=0;
	*end=0;
	return args;
}

//read number sections from string
int read_num_of_sections(char * args){
	int number=0;
	if (*args==',') args++;
	while (*args!=0 && *args!=','){
		if (*args==':') number++;
		args++;
	}
	return number+1; //+1 for the last section, which isn't closed with ':'
}

//dims leds
//brightness <channel>,<brightness>,<sections> (brightness: 0-255)
void brightness_sections(char * args){
	char value[MAX_VAL_LEN];
	char sections[MAX_VAL_LEN] = "0-50000";
	int channel=0, brightness=255;
	unsigned int start=0, end=0;
    if (is_valid_channel_number(channel)){
        end = ledstring.channel[channel].count;;
    }
	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)) end = ledstring.channel[channel].count;;
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			brightness = atoi(value);
						if (*args!=0){
							args = read_val(args, sections, MAX_VAL_LEN);
            }
		}
	}

	if (is_valid_channel_number(channel)){
        if (brightness<0 || brightness>0xFF) brightness=255;

        if (debug) printf("Changing brightness %d, %d, %s\n", channel, brightness, sections);

        ws2811_led_t * leds = ledstring.channel[channel].leds;
        unsigned int i,j;
				int num = read_num_of_sections(sections);
				char val1[MAX_VAL_LEN];
				char val2[MAX_VAL_LEN];
				for(j=0;j<num;j++){
					read_section(sections,j,val1,val2);
					start = atoi(val1);
					end = atoi(val2);
					if (start<0 || start>=ledstring.channel[channel].count) start=0;
					if(end<start) end=start;
					for (i=start;i<=end;i++){
	            leds[i].brightness=brightness;
	        }
				}
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//makes some leds blink between 2 given colors for x times with a given delay
//blink <channel>,<color1>,<color2>,<delay>,<blink_count>,<sections>
void blink_sections (char * args){
    char value[MAX_VAL_LEN];
	char sections[MAX_VAL_LEN] = "0-50000";
	int channel=0, color1=0, color2=0xFFFFFF,delay=1000, count=10;
	unsigned int start=0, end=0;

    if (is_valid_channel_number(channel)){
        end = ledstring.channel[channel].count;;
    }
    if (args!=NULL){
        args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
        if (is_valid_channel_number(channel)){
            end = ledstring.channel[channel].count;;
        }
        if (*args!=0){
            args = read_val(args, value, MAX_VAL_LEN);
			if (strlen(value)>=6){
                if (is_valid_channel_number(channel)) read_color(value, & color1, ledstring.channel[channel].color_size);
			}else{
				printf("Invalid color 1\n");
			}
            if(*args!=0){
                args = read_val(args, value, MAX_VAL_LEN);
				if (strlen(value)>=6){
					if (is_valid_channel_number(channel)) read_color(value, & color2, ledstring.channel[channel].color_size);
				}else{
					printf("Invalid color 2\n");
				}
                if(*args!=0){
                    args = read_val(args, value, MAX_VAL_LEN);
                    delay = atoi(value);
                    if(*args!=0){
                        args = read_val(args, value, MAX_VAL_LEN);
                        count = atoi(value);
			if (*args!=0){
				args = read_val(args, sections, MAX_VAL_LEN);
                        }
                    }
                }
            }
        }
    }

	if (is_valid_channel_number(channel)){

        if (delay<=0) delay=100;

        if (debug) printf("blink %d, %d, %d, %d, %d, %s\n", channel, color1, color2, delay, count, sections);

        ws2811_led_t * leds = ledstring.channel[channel].leds;
	int num = read_num_of_sections(sections);
	char val1[MAX_VAL_LEN];
	char val2[MAX_VAL_LEN];
        int i,j,blinks;
        for (blinks=0; blinks<count;blinks++){
		for(j=0;j<num;j++){
		read_section(sections,j,val1,val2);
		start = atoi(val1);
		end = atoi(val2);
		if (start<0 || start>=ledstring.channel[channel].count) start=0;
		if(end<start) end=start;
		if (end>ledstring.channel[channel].count) end=ledstring.channel[channel].count-start;
	            for (i=start;i<=end;i++){
			if ((blinks%2)==0) {
						leds[i].color=color1;
					}else{
						leds[i].color=color2;
					}
		    }
		}
            ws2811_render(&ledstring);
            usleep(delay * 1000);
        }
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}

//fills leds with certain color
//fill <channel>,<color>,<sections>,<OR,AND,XOR,NOT,=>
void fill_sections(char * args){
	char value[MAX_VAL_LEN];
	char sections[MAX_VAL_LEN] = "0-50000";
   	char op=0;
	int channel=0,start=0,end=-1;
	unsigned int fill_color=0;

	if (args!=NULL){
		args = read_val(args, value, MAX_VAL_LEN);
		channel = atoi(value)-1;
		if (*args!=0){
			args = read_val(args, value, MAX_VAL_LEN);
			if (strlen(value)>=6){
                if (is_valid_channel_number(channel)) read_color(value, & fill_color, ledstring.channel[channel].color_size);
			}else{
				printf("Invalid color\n");
			}
			if (*args!=0){
				args = read_val(args, sections, MAX_VAL_LEN);
				if (*args!=0){
					args = read_val(args, value, MAX_VAL_LEN);
					if (strcmp(value, "OR")==0) op=1;
					else if (strcmp(value, "AND")==0) op=2;
					else if (strcmp(value, "XOR")==0) op=3;
					else if (strcmp(value, "NOT")==0) op=4;
					else if (strcmp(value, "=")==0) op=0;
				}
			}
		}
	}

	if (is_valid_channel_number(channel)){

        if (debug) printf("fill %d,%d,%s,%d\n", channel, fill_color, sections,op);

        ws2811_led_t * leds = ledstring.channel[channel].leds;
	unsigned int i,j;
	int num = read_num_of_sections(sections);
	char val1[MAX_VAL_LEN];
	char val2[MAX_VAL_LEN];

	for(j=0;j<num;j++){
		read_section(sections,j,val1,val2);
		start = atoi(val1);
		end = atoi(val2);
		if (start<0 || start>=ledstring.channel[channel].count) start=0;
		if(end<start) end=start;
       		if (end>ledstring.channel[channel].count) end=ledstring.channel[channel].count-start;
	        for (i=start;i<=end;i++){
		    switch (op){
			case 0:
			    leds[i].color=fill_color;
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
			    leds[i].color=~leds[i].color;
			    break;
		    }
        	}
	}
    }else{
        fprintf(stderr,"Invalid channel number, did you call setup and init?\n");
    }
}
