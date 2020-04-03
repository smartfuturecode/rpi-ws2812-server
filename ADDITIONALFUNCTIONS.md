# How To Install Additional Functions

## Changes on main.code
* add `#include "additionalfunctions.h"` to main.c
* replace the function `fill` with `fill_sections` from additionalfunctions.c
* replace the function `blink` with `blink_sections` from additionalfunctions.c
* replace the function `brightness` with `brightness_sections` from additionalfunctions.c
* add the other functions from additionalfunctions.c to main.c
* within `void execute_command(char * command_line)` add
```C
else if (strcmp(command, "gpio")==0){
    gpio(arg);
}
```
to the if-else-structure
* add `
```C
#ifdef USE_WIRINGPI
  printf("gpio <pin>,<state>\n");
```
* add
```C
#ifdef USE_WIRINGPI
  init_wiringPi();
#endif
```
to `if (strcmp(command, "init")==0)0` within `void execute_command(char * command_line)`
* within `main()` add

```C
else if (strcmp(argv[arg_idx], "-artnet")==0){ //open up tcp ip port and read commands from there
    int port = 9998;
    system("sleep 5 && node hcu-artnet-server/main.js &");
    printf("Listening on %d.\n", port);
    start_tcpip(port);
    mode = MODE_TCP;
}
```

to the if-else-structure
* add `			printf("-artnet             listen for Artnet connection to receive commands from.\n");` to `if (strcmp(argv[arg_idx], "-?")==0)`
