#ifndef __INOUT_H__
#define __INOUT_H__

#define BCM2708_PERI_BASE 	(0x3F000000)	//decimal 1056964608
#define GPIO_BASE 		(BCM2708_PERI_BASE + 0x200000)//decimal 1059061760
#define BLOCK_SIZE 		(4096)

struct bcm2835_peripheral
{
    int mem_fd;
    void *map;
    volatile unsigned int *addr;//address of mapped area
};

int map_peripheral(struct bcm2835_peripheral *p);
void unmap_peripheral(struct bcm2835_peripheral *p)
void input_GPIO(int gpio_numb);
void output_GPIO(int gpio_numb);
void high_GPIO(int gpio_numb);
void low_GPIO(int gpio_numb);

#endif