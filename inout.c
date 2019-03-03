#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mmap.h>

//mmap function
int map_peripheral(struct bcm2835_peripheral *p)
{
    //open the /dev/mem folder with read/write
    if((p->mem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0)
    {
        printf("Failed to open /dem/mem, did you sudo?\n");
        return -1;
    }
    p->map = mmap(NULL,
                  BLOCK_SIZE,
                  PROT_READ|PROT_WRITE,
                  MAP_SHARED,
                  p>mem_fd,
                  GPIO_BASE);

    if(p->map == MAP_FAILED)
    {
        printf("mmap failed, MAP_FAILED\n");
        return -1;
    }
    close(p->mem_fd);
    p->addr = (volatile unsigned int *)p->map;

    return 0;
}
//unmap the same memory block
void unmap_peripheral(struct bcm2835_peripheral *p)
{
    munmap(p->map, BLOCK_SIZE);
    close(p->mem_fd);
}


//here is the GPIO manipulation in CLEAN C
//setting the pin as input
void input_GPIO(int gpio_numb)
{
    *(gpio.addr + ((gpio_numb)/10)) &= ~(7<<(((gpio_numb)%10)*3));
}
//setting the pin as output
void output_GPIO(int gpio_numb)
{
    input_GPIO(gpio_numb);
    *(gpio.addr + ((gpio_numb)/10)) |=  (1<<(((gpio_numb)%10)*3));
}
//setting the pin to HIGH
void high_GPIO(int gpio_numb)
{
    *(gpio.addr + 7) = 1 << gpio_numb;
}
//setting the pin to LOW
void low_GPIO(int gpio_numb)
{
    *(gpio.addr + 10) = 1 << gpio_numb;
}