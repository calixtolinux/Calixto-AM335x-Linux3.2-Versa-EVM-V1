/*
 * TI Touch Screen driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/input/ti_tscadc.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#define TSCADC_REG_IRQEOI		0x020
#define TSCADC_REG_RAWIRQSTATUS		0x024
#define TSCADC_REG_IRQSTATUS		0x028
#define TSCADC_REG_IRQENABLE		0x02C
#define TSCADC_REG_IRQCLR		0x030
#define TSCADC_REG_IRQWAKEUP		0x034
#define TSCADC_REG_CTRL			0x040
#define TSCADC_REG_ADCFSM		0x044
#define TSCADC_REG_CLKDIV		0x04C
#define TSCADC_REG_SE			0x054
#define TSCADC_REG_IDLECONFIG		0x058
#define TSCADC_REG_CHARGECONFIG		0x05C
#define TSCADC_REG_CHARGEDELAY		0x060
#define TSCADC_REG_STEPCONFIG(n)	(0x64 + ((n-1) * 8))
#define TSCADC_REG_STEPDELAY(n)		(0x68 + ((n-1) * 8))
#define TSCADC_REG_FIFO0CNT			0xE4
#define TSCADC_REG_FIFO0THR			0xE8
#define TSCADC_REG_FIFO1CNT			0xF0
#define TSCADC_REG_FIFO1THR			0xF4
#define TSCADC_REG_FIFO0			0x100
#define TSCADC_REG_FIFO1			0x200

/*	Register Bitfields	*/
#define TSCADC_IRQWKUP_ENB		BIT(0)
#define TSCADC_IRQWKUP_DISABLE		0x00
#define TSCADC_STPENB_STEPENB_TSC	0x01FFF
#define TSCADC_STPENB_STEPENB_ADC	0x1E000
#define TSCADC_IRQENB_FIFO0_THRES	BIT(2)
#define TSCADC_IRQENB_FIFO0_OVER	BIT(3)
#define TSCADC_IRQENB_FIFO0_UNDER	BIT(4)
#define TSCADC_IRQENB_FIFO1_THRES	BIT(5)
#define TSCADC_IRQENB_FIFO1_OVER	BIT(6)
#define TSCADC_IRQENB_FIFO1_UNDER	BIT(7)
#define TSCADC_IRQENB_PENUP             BIT(9)
#define TSCADC_IRQENB_HW_PEN		BIT(0)
#define TSCADC_STEPCONFIG_MODE_SW_ONESHOT	0x0
#define TSCADC_STEPCONFIG_MODE_SW_CONTINUE	0x1
#define TSCADC_STEPCONFIG_MODE_HWSYNC_ONESHOT	0x2
#define TSCADC_STEPCONFIG_MODE_HWSYNC_CONTINUE	0x3
#define TSCADC_STEPCONFIG_2SAMPLES_AVG	(1 << 2)
#define TSCADC_STEPCONFIG_4SAMPLES_AVG	(2 << 2)
#define TSCADC_STEPCONFIG_8SAMPLES_AVG	(3 << 2)
#define TSCADC_STEPCONFIG_16SAMPLES_AVG	(4 << 2)
#define TSCADC_STEPCONFIG_XPP		BIT(5)
#define TSCADC_STEPCONFIG_XNN		BIT(6)
#define TSCADC_STEPCONFIG_YPP		BIT(7)
#define TSCADC_STEPCONFIG_YNN		BIT(8)
#define TSCADC_STEPCONFIG_XNP		BIT(9)
#define TSCADC_STEPCONFIG_YPN		BIT(10)
#define TSCADC_STEPCONFIG_RFP		(1 << 12)
#define TSCADC_STEPCONFIG_INM		(1 << 18)
#define TSCADC_STEPCONFIG_INP_4		(1 << 19)
#define TSCADC_STEPCONFIG_INP		(1 << 20)
#define TSCADC_STEPCONFIG_INP_5		(1 << 21)
#define TSCADC_STEPCONFIG_FIFO1		(1 << 26)
#define TSCADC_STEPCONFIG_FIFO0		0
#define TSCADC_STEPCONFIG_INP_SWC(i) (i << 19)
#define TSCADC_STEPCONFIG_INM_SWC(i) (i << 15)
#define TSCADC_STEPCONFIG_INM_SWC_VREFN  (0x8 << 15)
#define TSCADC_STEPCONFIG_IDLE_INP	(1 << 22)
#define TSCADC_STEPCONFIG_OPENDLY	0x018
#define TSCADC_STEPCONFIG_SAMPLEDLY	0x88
#define TSCADC_STEPCONFIG_Z1		(3 << 19)
#define TSCADC_STEPCHARGE_INM_SWAP	BIT(16)
#define TSCADC_STEPCHARGE_INM		BIT(15)
#define TSCADC_STEPCHARGE_INP_SWAP	BIT(20)
#define TSCADC_STEPCHARGE_INP		BIT(19)
#define TSCADC_STEPCHARGE_RFM		(1 << 23)
#define TSCADC_STEPCHARGE_DELAY		0x1
#define TSCADC_CNTRLREG_TSCSSENB	BIT(0)
#define TSCADC_CNTRLREG_STEPID		BIT(1)
#define TSCADC_CNTRLREG_STEPCONFIGWRT	BIT(2)
#define TSCADC_CNTRLREG_POWERDOWN	BIT(4)
#define TSCADC_CNTRLREG_TSCENB		BIT(7)
#define TSCADC_CNTRLREG_4WIRE		(0x1 << 5)
#define TSCADC_CNTRLREG_5WIRE		(0x1 << 6)
#define TSCADC_CNTRLREG_8WIRE		(0x3 << 5)
#define TSCADC_ADCFSM_STEPID		0x10
#define TSCADC_ADCFSM_FSM		BIT(5)

#define ADC_CLK				3000000

#define MAX_12BIT                       ((1 << 12) - 1)

static int pen = 1;
static unsigned int bckup_x = 0, bckup_y = 0;

struct tscadc {
	struct input_dev	*input;
	int			wires;
	int			x_plate_resistance;
	int			irq;
	void __iomem		*tsc_base;
	unsigned int		ctrl;
	int			adc_enabled;
	unsigned int		adc_capture_period_ms;
};


static unsigned short tsc_adc_val[4];
static void adc_capture_timer_func(unsigned long data);
static DEFINE_TIMER(adc_capture_timer, adc_capture_timer_func, 0, 0);

static unsigned int tscadc_readl(struct tscadc *ts, unsigned int reg)
{
	return readl(ts->tsc_base + reg);
}

static void tscadc_writel(struct tscadc *tsc, unsigned int reg,
					unsigned int val)
{
	writel(val, tsc->tsc_base + reg);
}

static void adc_capture_timer_func(unsigned long data)
{
	struct tscadc *ts_dev	= (struct tscadc *)data;
	
	/* Enable ADC Mode stepconfigs */
	tscadc_writel(ts_dev, TSCADC_REG_SE, 
				tscadc_readl(ts_dev, TSCADC_REG_SE) | TSCADC_STPENB_STEPENB_ADC);

    /* add Periodic timer with default sampling rate */
	mod_timer(&adc_capture_timer, 
				jiffies + msecs_to_jiffies(ts_dev->adc_capture_period_ms));

	return;	
}

static void tsc_step_config_adc(struct tscadc *ts_dev)
{

	unsigned int	stepconfig = 0;
	unsigned int	delay = 0;

	delay = TSCADC_STEPCONFIG_SAMPLEDLY | TSCADC_STEPCONFIG_OPENDLY;
	
	/* Configure the Step registers */
	stepconfig = TSCADC_STEPCONFIG_MODE_SW_ONESHOT |
			TSCADC_STEPCONFIG_16SAMPLES_AVG |
			TSCADC_STEPCONFIG_FIFO0;

	/* Step-13 for AIN4 */
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(14), stepconfig | 
					TSCADC_STEPCONFIG_INP_SWC(4) |
					TSCADC_STEPCONFIG_INM_SWC_VREFN);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(14), delay);

	/* Step-14 for AIN5 */
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(14), stepconfig | 
					TSCADC_STEPCONFIG_INP_SWC(5) |
					TSCADC_STEPCONFIG_INM_SWC_VREFN);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(14), delay);

	/* Step-15 for AIN6 */
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(15), stepconfig | 
					TSCADC_STEPCONFIG_INP_SWC(6) |
					TSCADC_STEPCONFIG_INM_SWC_VREFN);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(15), delay);

	/* Step-16 for AIN7 */
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(16), stepconfig | 
					TSCADC_STEPCONFIG_INP_SWC(7) |
					TSCADC_STEPCONFIG_INM_SWC_VREFN);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(16), delay);

	/* Enable ADC Mode stepconfigs */
	tscadc_writel(ts_dev, TSCADC_REG_SE, 
				tscadc_readl(ts_dev, TSCADC_REG_SE) |
				TSCADC_STPENB_STEPENB_ADC);
	
	return;
}

static void tsc_step_config_tsc(struct tscadc *ts_dev)
{
	unsigned int	stepconfigx = 0, stepconfigy = 0;
	unsigned int	delay, chargeconfig = 0;
	unsigned int	stepconfigz1 = 0, stepconfigz2 = 0;
	int i;

	/* Configure the Step registers */

	delay = TSCADC_STEPCONFIG_SAMPLEDLY | TSCADC_STEPCONFIG_OPENDLY;

	
	stepconfigx = TSCADC_STEPCONFIG_MODE_HWSYNC_ONESHOT |
			TSCADC_STEPCONFIG_16SAMPLES_AVG | TSCADC_STEPCONFIG_XPP |
			TSCADC_STEPCONFIG_FIFO1;
 
	stepconfigy = TSCADC_STEPCONFIG_MODE_HWSYNC_ONESHOT |
			TSCADC_STEPCONFIG_16SAMPLES_AVG | TSCADC_STEPCONFIG_YNN |
			TSCADC_STEPCONFIG_INM | TSCADC_STEPCONFIG_FIFO1;

	switch (ts_dev->wires) {
	case 4:
		stepconfigx |= TSCADC_STEPCONFIG_INP |
				TSCADC_STEPCONFIG_XNN;
		stepconfigy |= TSCADC_STEPCONFIG_YPP;
		break;
	case 5:
		stepconfigx |= TSCADC_STEPCONFIG_YNN |
				TSCADC_STEPCONFIG_INP_5 | TSCADC_STEPCONFIG_XNN |
				TSCADC_STEPCONFIG_YPP;
		stepconfigy |= TSCADC_STEPCONFIG_XPP | TSCADC_STEPCONFIG_INP_5 |
			TSCADC_STEPCONFIG_XNP | TSCADC_STEPCONFIG_YPN;
	break;
	case 8:
		stepconfigx |= TSCADC_STEPCONFIG_INP |
				TSCADC_STEPCONFIG_XNN;
		stepconfigy |= TSCADC_STEPCONFIG_YPP;
		break;
	}

	for (i = 1; i < 6; i++) {
		tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(i), stepconfigx);
		tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(i), delay);
	}
	for (i = 6; i < 11; i++) {
		tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(i), stepconfigy);
		tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(i), delay);
	}

	chargeconfig = TSCADC_STEPCONFIG_XPP | TSCADC_STEPCONFIG_YNN |
			TSCADC_STEPCONFIG_RFP | TSCADC_STEPCHARGE_RFM |
			TSCADC_STEPCHARGE_INM | TSCADC_STEPCHARGE_INP;
	tscadc_writel(ts_dev, TSCADC_REG_CHARGECONFIG, chargeconfig);
	tscadc_writel(ts_dev, TSCADC_REG_CHARGEDELAY, TSCADC_STEPCHARGE_DELAY);

	 /* Configure to calculate pressure */
	stepconfigz1 = TSCADC_STEPCONFIG_MODE_HWSYNC_ONESHOT |
				TSCADC_STEPCONFIG_16SAMPLES_AVG |
				TSCADC_STEPCONFIG_XNP |
				TSCADC_STEPCONFIG_YPN | TSCADC_STEPCONFIG_INM |
				TSCADC_STEPCONFIG_FIFO1;
	stepconfigz2 = stepconfigz1 | TSCADC_STEPCONFIG_Z1 |
				TSCADC_STEPCONFIG_FIFO1;
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(11), stepconfigz1);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(11), delay);
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG(12), stepconfigz2);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY(12), delay);

	/* Enable TSC Mode stepconfigs */
	tscadc_writel(ts_dev, TSCADC_REG_SE, 
				tscadc_readl(ts_dev, TSCADC_REG_SE) |
				TSCADC_STPENB_STEPENB_TSC);
}

static void tsc_idle_config(struct tscadc *ts_config)
{
	/* Idle mode touch screen config */
	unsigned int	 idleconfig;

	idleconfig = TSCADC_STEPCONFIG_YNN | TSCADC_STEPCONFIG_INM |
			TSCADC_STEPCONFIG_IDLE_INP | TSCADC_STEPCONFIG_YPN;

	tscadc_writel(ts_config, TSCADC_REG_IDLECONFIG, idleconfig);
}

static irqreturn_t tscadc_interrupt(int irq, void *dev)
{
	struct tscadc		*ts_dev = (struct tscadc *)dev;
	struct input_dev	*input_dev = ts_dev->input;
	unsigned int		status, irqclr = 0;
	int			i;
	int			fsm = 0, fifo0count = 0, fifo1count = 0;
	unsigned int		readx1 = 0, ready1 = 0;
	unsigned int		prev_val_x = ~0, prev_val_y = ~0;
	unsigned int		prev_diff_x = ~0, prev_diff_y = ~0;
	unsigned int		cur_diff_x = 0, cur_diff_y = 0;
	unsigned int		val_x = 0, val_y = 0, diffx = 0, diffy = 0;
	unsigned int		z1 = 0, z2 = 0, z = 0;

	/* Get TSC IRQ status */
	status = tscadc_readl(ts_dev, TSCADC_REG_IRQSTATUS);

	/* Check FIFO 0 Thresold IRQ, it's trigger when General Purpose ADC Conversion completed */
	if (status & TSCADC_IRQENB_FIFO0_THRES) {
		fifo0count = tscadc_readl(ts_dev, TSCADC_REG_FIFO0CNT);
	
		/* get ADC sample values from FIFO 0 */
		for(i=0; i < 4; i++)
			tsc_adc_val[i] = tscadc_readl(ts_dev, TSCADC_REG_FIFO0) & 0xFFF;	
		
		/* Clear FIFO-0 threshold Interrupt */
		tscadc_writel(ts_dev, TSCADC_REG_IRQSTATUS, TSCADC_IRQENB_FIFO0_THRES ); 
	}

	/* Check FIF1 0 Thresold IRQ, it's came when General Purpose TSC Conversion completed */
	if (status & TSCADC_IRQENB_FIFO1_THRES) {
		fifo1count = tscadc_readl(ts_dev, TSCADC_REG_FIFO1CNT);

		/* First get the X samples from FIFO */
		for (i = 0; i < 5; i++) {
			readx1 = tscadc_readl(ts_dev, TSCADC_REG_FIFO1);
			readx1 = readx1 & 0xfff;
			if (readx1 > prev_val_x)
				cur_diff_x = readx1 - prev_val_x;
			else
				cur_diff_x = prev_val_x - readx1;

			if (cur_diff_x < prev_diff_x) {
				prev_diff_x = cur_diff_x;
				val_x = readx1;
			}

			prev_val_x = readx1;
		}

		/* Now get Y samples from FIFO */
		for (i = 0; i < 5; i++) {
			ready1 = tscadc_readl(ts_dev, TSCADC_REG_FIFO1);
				ready1 &= 0xfff;
			if (ready1 > prev_val_y)
				cur_diff_y = ready1 - prev_val_y;
			else
				cur_diff_y = prev_val_y - ready1;

			if (cur_diff_y < prev_diff_y) {
				prev_diff_y = cur_diff_y;
				val_y = ready1;
			}

			prev_val_y = ready1;
		}

		if (val_x > bckup_x) {
			diffx = val_x - bckup_x;
			diffy = val_y - bckup_y;
		} else {
			diffx = bckup_x - val_x;
			diffy = bckup_y - val_y;
		}
		bckup_x = val_x;
		bckup_y = val_y;

		z1 = ((tscadc_readl(ts_dev, TSCADC_REG_FIFO1)) & 0xfff);
		z2 = ((tscadc_readl(ts_dev, TSCADC_REG_FIFO1)) & 0xfff);

		if ((z1 != 0) && (z2 != 0)) {
			/*
			 * cal pressure using formula
			 * Resistance(touch) = x plate resistance *
			 * x postion/4096 * ((z2 / z1) - 1)
			 */
			z = z2 - z1;
			z *= val_x;
			z *= ts_dev->x_plate_resistance;
			z /= z1;
			z = (z + 2047) >> 12;

			/*
			 * Sample found inconsistent by debouncing
			 * or pressure is beyond the maximum.
			 * Don't report it to user space.
			 */
			if (pen == 0) {
				if ((diffx < 15) && (diffy < 15)
						&& (z <= MAX_12BIT)) {
					input_report_abs(input_dev, ABS_X,
							val_x);
					input_report_abs(input_dev, ABS_Y,
							val_y);
					input_report_abs(input_dev, ABS_PRESSURE,
							z);
					input_report_key(input_dev, BTN_TOUCH,
							1);
					input_sync(input_dev);
				}
			}
		}
		irqclr |= TSCADC_IRQENB_FIFO1_THRES;

		udelay(315);

		status = tscadc_readl(ts_dev, TSCADC_REG_RAWIRQSTATUS);
		if (status & TSCADC_IRQENB_PENUP) {
			/* Pen up event */
		fsm = tscadc_readl(ts_dev, TSCADC_REG_ADCFSM);
			if (fsm == 0x10) {
				pen = 1;
				bckup_x = 0;
				bckup_y = 0;
				input_report_key(input_dev, BTN_TOUCH, 0);
				input_report_abs(input_dev, ABS_PRESSURE, 0);
				input_sync(input_dev);
			} else {
				pen = 0;
			}
			irqclr |= TSCADC_IRQENB_PENUP;
	}

	irqclr |= TSCADC_IRQENB_HW_PEN;

	tscadc_writel(ts_dev, TSCADC_REG_IRQSTATUS, irqclr);
	}
	/* check pending interrupts */
	tscadc_writel(ts_dev, TSCADC_REG_IRQEOI, 0x0);

	/* If we got the TSC FIFO interrupt, reenable the TSC Steps */
	if(irqclr & TSCADC_IRQENB_FIFO1_THRES)
	{
		tscadc_writel(ts_dev, TSCADC_REG_SE, 
							tscadc_readl(ts_dev, TSCADC_REG_SE) |
							TSCADC_STPENB_STEPENB_TSC);
	}

	return IRQ_HANDLED;
}

/* Create sysfs interface for 4 Genral Purpose ADC Channels */
static ssize_t tsc_adc_channel_1_show_val(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%u\n", tsc_adc_val[0]);
}

static ssize_t tsc_adc_channel_2_show_val(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%u\n", tsc_adc_val[1]);
}

static ssize_t tsc_adc_channel_3_show_val(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%u\n", tsc_adc_val[2]);
}

static ssize_t tsc_adc_channel_4_show_val(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%u\n", tsc_adc_val[3]);
}

static ssize_t tsc_adc_capture_period_ms_show_val(struct device *dev,
                struct device_attribute *attr, char *buf)
{
		struct tscadc *ts_dev = (struct tscadc*)dev_get_drvdata(dev);

        return sprintf(buf, "%u\n", ts_dev->adc_capture_period_ms);
}

static ssize_t tsc_adc_capture_period_ms_store_val(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t count)
{
		struct tscadc *ts_dev = (struct tscadc*)dev_get_drvdata(dev);

        ts_dev->adc_capture_period_ms = simple_strtoul(buf, NULL, 10);

		/* Make sure adc capturing rate more than 100ms */
		if(100 > ts_dev->adc_capture_period_ms)
			ts_dev->adc_capture_period_ms = 100;

        return count;
}

static DEVICE_ATTR(adc_capture_period_ms, S_IWUSR | S_IRUGO,tsc_adc_capture_period_ms_show_val , tsc_adc_capture_period_ms_store_val );
static DEVICE_ATTR(channel_1, S_IRUGO, tsc_adc_channel_1_show_val, NULL);
static DEVICE_ATTR(channel_2, S_IRUGO, tsc_adc_channel_2_show_val, NULL);
static DEVICE_ATTR(channel_3, S_IRUGO, tsc_adc_channel_3_show_val, NULL);
static DEVICE_ATTR(channel_4, S_IRUGO, tsc_adc_channel_4_show_val, NULL);

static struct attribute *tsc_adc_attributes[] = {
        &dev_attr_adc_capture_period_ms.attr,
        &dev_attr_channel_1.attr,
        &dev_attr_channel_2.attr,
        &dev_attr_channel_3.attr,
        &dev_attr_channel_4.attr,
        NULL
};

static const struct attribute_group tsc_adc_attr_group = {
        .attrs = tsc_adc_attributes,
};

/*
* The functions for inserting/removing driver as a module.
*/

static	int __devinit tscadc_probe(struct platform_device *pdev)
{
	struct tscadc			*ts_dev;
	struct input_dev		*input_dev;
	int				err;
	int				clk_value;
	int				clock_rate, irqenable, ctrl;
	struct	tsc_data		*pdata = pdev->dev.platform_data;
	struct resource			*res;
	struct clk			*clk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined.\n");
		return -EINVAL;
	}

	/* Allocate memory for device */
	ts_dev = kzalloc(sizeof(struct tscadc), GFP_KERNEL);
	if (!ts_dev) {
		dev_err(&pdev->dev, "failed to allocate memory.\n");
		return -ENOMEM;
	}

	ts_dev->irq = platform_get_irq(pdev, 0);
	if (ts_dev->irq < 0) {
		dev_err(&pdev->dev, "no irq ID is specified.\n");
		return -ENODEV;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device.\n");
		err = -ENOMEM;
		goto err_free_mem;
	}
	ts_dev->input = input_dev;

	res =  request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to reserve registers.\n");
		err = -EBUSY;
		goto err_free_mem;
	}

	ts_dev->tsc_base = ioremap(res->start, resource_size(res));
	if (!ts_dev->tsc_base) {
		dev_err(&pdev->dev, "failed to map registers.\n");
		err = -ENOMEM;
		goto err_release_mem;
	}

	err = request_irq(ts_dev->irq, tscadc_interrupt, IRQF_DISABLED,
				pdev->dev.driver->name, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_unmap_regs;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	clk = clk_get(&pdev->dev, "adc_tsc_fck");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get TSC fck\n");
		err = PTR_ERR(clk);
		goto err_free_irq;
	}
	clock_rate = clk_get_rate(clk);
	clk_put(clk);
	clk_value = clock_rate / ADC_CLK;
	if (clk_value < 7) {
		dev_err(&pdev->dev, "clock input less than min clock requirement\n");
		err = -EINVAL;
		goto err_fail;
	}
	/* TSCADC_CLKDIV needs to be configured to the value minus 1 */
	clk_value = clk_value - 1;
	tscadc_writel(ts_dev, TSCADC_REG_CLKDIV, clk_value);

	ts_dev->wires = pdata->wires;
	ts_dev->x_plate_resistance = pdata->x_plate_resistance;

	ts_dev->adc_enabled = pdata->adc_enabled;
	ts_dev->adc_capture_period_ms = pdata->adc_capture_period_ms;
	
	/* Set the control register bits */
	ctrl = TSCADC_CNTRLREG_STEPCONFIGWRT |
			TSCADC_CNTRLREG_TSCENB |
			TSCADC_CNTRLREG_STEPID;
	switch (ts_dev->wires) {
	case 4:
		ctrl |= TSCADC_CNTRLREG_4WIRE;
		break;
	case 5:
		ctrl |= TSCADC_CNTRLREG_5WIRE;
		break;
	case 8:
		ctrl |= TSCADC_CNTRLREG_8WIRE;
		break;
	}
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, ctrl);
	ts_dev->ctrl = ctrl;

	/* Set register bits for Idel Config Mode */
	tsc_idle_config(ts_dev);

	/* IRQ Enable  FIFO 1*/
	irqenable = TSCADC_IRQENB_FIFO1_THRES;
	tscadc_writel(ts_dev, TSCADC_REG_IRQENABLE, irqenable);

	/* Configure required steps for TSC Mode */
	tsc_step_config_tsc(ts_dev);
	
	/* Set FIFO 1 Threshold for TSC Mode , 12 Steps*/
	tscadc_writel(ts_dev, TSCADC_REG_FIFO1THR, 11);

	/* If TSC Module remaining ADC's are enabled as General purpose ADC */
	if(ts_dev->adc_enabled && (4 == ts_dev->wires)) {
		/* IRQ Enable  FIFO 0*/
		tscadc_writel(ts_dev, TSCADC_REG_IRQENABLE, TSCADC_IRQENB_FIFO0_THRES);

		/* Configure required steps for ADC Mode , 4 Steps*/
		tsc_step_config_adc(ts_dev);
	
		/* Set FIFO 0 Threshold for ADC Mode */
		tscadc_writel(ts_dev, TSCADC_REG_FIFO0THR, 3);

		/* Make sure that capture period time more than 100ms */
		if(100 > ts_dev->adc_capture_period_ms)
			ts_dev->adc_capture_period_ms = 100;

		/* add Periodic timer with given capture period */
		init_timer(&adc_capture_timer);
		adc_capture_timer.function = adc_capture_timer_func;
		adc_capture_timer.expires = jiffies + msecs_to_jiffies(ts_dev->adc_capture_period_ms);
		adc_capture_timer.data = (unsigned long)ts_dev;
		add_timer(&adc_capture_timer);

		/* Register sysfs hooks */
		if( sysfs_create_group(&pdev->dev.kobj, &tsc_adc_attr_group)) {
			dev_err(&pdev->dev, "syfs hook Failed\n");
			goto err_fail;
		}
	}

	ctrl |= TSCADC_CNTRLREG_TSCSSENB;
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, ctrl);

	input_dev->name = "ti-tsc-adcc";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT, 0, 0);

	/* register to the input system */
	err = input_register_device(input_dev);
	if (err)
		goto err_fail;

	device_init_wakeup(&pdev->dev, true);
	platform_set_drvdata(pdev, ts_dev);

	return 0;

err_fail:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
err_free_irq:
	free_irq(ts_dev->irq, ts_dev);
err_unmap_regs:
	iounmap(ts_dev->tsc_base);
err_release_mem:
	release_mem_region(res->start, resource_size(res));
	input_free_device(ts_dev->input);
err_free_mem:
	platform_set_drvdata(pdev, NULL);
	kfree(ts_dev);
	return err;
}

static int __devexit tscadc_remove(struct platform_device *pdev)
{
	struct tscadc		*ts_dev = platform_get_drvdata(pdev);
	struct resource		*res;


	if(ts_dev->adc_enabled && (4 == ts_dev->wires))
		del_timer_sync(&adc_capture_timer);

	tscadc_writel(ts_dev, TSCADC_REG_SE, 0x00);
	free_irq(ts_dev->irq, ts_dev);

	input_unregister_device(ts_dev->input);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap(ts_dev->tsc_base);
	release_mem_region(res->start, resource_size(res));

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	kfree(ts_dev);

	device_init_wakeup(&pdev->dev, 0);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int tscadc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tscadc *ts_dev = platform_get_drvdata(pdev);
	unsigned int idle;

	if (device_may_wakeup(&pdev->dev)) {
		idle = tscadc_readl(ts_dev, TSCADC_REG_IRQENABLE);
		tscadc_writel(ts_dev, TSCADC_REG_IRQENABLE,
				(idle | TSCADC_IRQENB_HW_PEN));
		tscadc_writel(ts_dev, TSCADC_REG_SE, 0x00);
		tscadc_writel(ts_dev, TSCADC_REG_IRQWAKEUP, TSCADC_IRQWKUP_ENB);
	} else {
	/* module disable */
	idle = tscadc_readl(ts_dev, TSCADC_REG_CTRL);
	idle &= ~(TSCADC_CNTRLREG_TSCSSENB);
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, (idle |
				TSCADC_CNTRLREG_POWERDOWN));
	}

	pm_runtime_put_sync(&pdev->dev);

	return 0;

}

static int tscadc_resume(struct platform_device *pdev)
{
	struct tscadc *ts_dev = platform_get_drvdata(pdev);
	unsigned int restore;

	pm_runtime_get_sync(&pdev->dev);

	if (device_may_wakeup(&pdev->dev)) {
		tscadc_writel(ts_dev, TSCADC_REG_IRQWAKEUP,
				TSCADC_IRQWKUP_DISABLE);
		tscadc_writel(ts_dev, TSCADC_REG_IRQCLR, TSCADC_IRQENB_HW_PEN);
	}

	/* context restore */
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, ts_dev->ctrl);
	/* Make sure ADC is powered up */
	restore = tscadc_readl(ts_dev, TSCADC_REG_CTRL);
	restore &= ~(TSCADC_CNTRLREG_POWERDOWN);
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, restore);
	tsc_idle_config(ts_dev);

	tsc_step_config_tsc(ts_dev);
	tscadc_writel(ts_dev, TSCADC_REG_FIFO1THR, 11);
 
	/* If General purpose ADC enabled */
	if(ts_dev->adc_enabled && (4 == ts_dev->wires)) {
		tsc_step_config_adc(ts_dev);
		tscadc_writel(ts_dev, TSCADC_REG_FIFO0THR, 3);
	}
	restore = tscadc_readl(ts_dev, TSCADC_REG_CTRL);
	tscadc_writel(ts_dev, TSCADC_REG_CTRL,
			(restore | TSCADC_CNTRLREG_TSCSSENB));

	return 0;
}

static struct platform_driver ti_tsc_driver = {
	.probe	  = tscadc_probe,
	.remove	 = __devexit_p(tscadc_remove),
	.driver	 = {
		.name   = "tsc",
		.owner  = THIS_MODULE,
	},
	.suspend = tscadc_suspend,
	.resume  = tscadc_resume,
};

static int __init ti_tsc_init(void)
{
	return platform_driver_register(&ti_tsc_driver);
}
module_init(ti_tsc_init);

static void __exit ti_tsc_exit(void)
{
	platform_driver_unregister(&ti_tsc_driver);
}
module_exit(ti_tsc_exit);

MODULE_DESCRIPTION("TI touchscreen controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
