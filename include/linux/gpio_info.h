#ifndef _GPIO_INFO_H
#define _GPIO_INFO_H

#define INPUT 	0
#define OUTPUT  1

#define LOW	0
#define HIGH	1

#define GPIO_TO_PIN(bank, gpio) (32 * (bank) + (gpio))

struct gpio_info_button {
        /* Configuration */
        int gpio;
        int direction;
	int id;
	char *name;
	int default_level;
	int debounce_interval;  /* debounce ticks interval in msecs */
};

struct gpio_info_platform_data {
	/* Button Info */
	struct	gpio_info_button *buttons;
	int    	nbuttons;
};

#endif
