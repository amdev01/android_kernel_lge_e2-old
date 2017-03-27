#ifndef _LINUX_MIT_TOUCH_H
#define _LINUX_MIT_TOUCH_H

struct mit_ts_platform_data {
	int	max_x;
	int	max_y;
/*         */
	int	gpio_irq;
	int gpio_reset;
	unsigned int irq_flags;
	unsigned int reset_flags;
	char *name;
	unsigned int gpio_17;
/*       */
/*	int	gpio_int;
 *	int	gpio_vdd;
 */
};

#endif /* _LINUX_MIT_TOUCH_H */
