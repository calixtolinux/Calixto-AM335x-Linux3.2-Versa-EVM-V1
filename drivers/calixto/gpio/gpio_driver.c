#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/gpio_info.h>
#include <linux/unistd.h>
#include <linux/timer.h>
#include <linux/sched.h>

#include <asm/uaccess.h>

#define GPIO_DRIVER_MAJOR       160
#define GPIO_DRIVER_MINOR	0	
#define NUMBER_OF_DEVICE	1 

#define EVM_IOC_MAGIC  'c'
#define START_EVM_IOCTL_OUT	_IOW(EVM_IOC_MAGIC, 1, int)
#define START_EVM_IOCTL_IN	_IOR(EVM_IOC_MAGIC, 2, struct gpio_info *)

struct user_input_data {
        int id;
        int value;
};

struct evm_gpio_data {
	dev_t        	  	devt;
	struct cdev  	  	cdev;
	struct class  	 	*class;
        struct device		*dev;
	struct user_input_data	user_data;	
	spinlock_t		sp_lock;
};

static struct platform_device *evm_gpio_data;
/*-------------------------------------------------------------------------*/
/*             GPIO IOCTL Call				                   */
/*-------------------------------------------------------------------------*/
static long gpio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int			err    = 0;
	int			retval = 0;
	unsigned int		ijk    = 0;

	struct evm_gpio_data   *gpdev = filp->private_data;
	struct platform_device *pdev  = to_platform_device(gpdev->dev);

	struct gpio_info_platform_data *gpdata    = pdev->dev.platform_data;
        struct gpio_info_button        *gpbuttons = gpdata->buttons;
	
	
	/* Check type and command number */
	if (_IOC_TYPE(cmd) != EVM_IOC_MAGIC)
		return -ENOTTY;

	/* Check access direction once here; don't repeat below.
	 * IOC_DIR is from the user perspective, while access_ok is
	 * from the kernel perspective; so they look reversed.
	 */
	if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	switch(cmd){

	   case START_EVM_IOCTL_OUT:

	   	err = copy_from_user(&gpdev->user_data, (struct user_input_data *)arg, sizeof(struct user_input_data));
		spin_lock(&gpdev->sp_lock);

		for (ijk = 0; ijk < gpdata->nbuttons; ijk++){

          		if (gpbuttons[ijk].id == gpdev->user_data.id){
               			printk("Found ID: %d\n", gpbuttons[ijk].id);
               			gpio_direction_output(gpbuttons[ijk].gpio, gpdev->user_data.value);
               			 printk("Set GPIO Name: %s to level: %d\n", gpbuttons[ijk].name, gpdev->user_data.value);
               			break;
	       		}
    		}
		
		spin_unlock(&gpdev->sp_lock);

	   break;

	   case START_EVM_IOCTL_IN:

		err = copy_from_user(&gpdev->user_data, (struct user_input_data *)arg, sizeof(struct user_input_data));	
		spin_lock(&gpdev->sp_lock);

		for (ijk = 0; ijk < gpdata->nbuttons; ijk++){

          		if (gpbuttons[ijk].id == gpdev->user_data.id){
               			printk("Found ID: %d\n", gpbuttons[ijk].id);
               			gpdev->user_data.value = gpio_get_value(gpbuttons[ijk].gpio);
               			printk("GPIO Name: %s has Input level: %d\n", gpbuttons[ijk].name, gpdev->user_data.value);
              		 	break;
                        }
    		}
		
		spin_unlock(&gpdev->sp_lock);
		err = copy_to_user((struct user_input_data *)arg, &gpdev->user_data, sizeof(struct user_input_data));
           
	   break;
	}	
	return retval;
}

static int gpio_open(struct inode *inode, struct file *file)
{
   struct evm_gpio_data *gpdev = platform_get_drvdata(evm_gpio_data);
   
          file->private_data = (void *) gpdev;
          return nonseekable_open(inode, file);
}

static int gpio_release(struct inode *inode, struct file *file)
{
        return 0;
}

/*-------------------------------------------------------------------------*/
/*             File_Operation                                              */
/*-------------------------------------------------------------------------*/
static const struct file_operations gpio_fops = {
        .owner          = THIS_MODULE,
        .unlocked_ioctl = gpio_ioctl,
        .open           = gpio_open,
        .release        = gpio_release,
};

/*-------------------------------------------------------------------------*/
/*             Probe for Register the GPIO_Driver		           */
/*-------------------------------------------------------------------------*/
static int __devinit gpio_probe(struct platform_device *pdev)
{
        struct gpio_info_platform_data *gpio_pdata = pdev->dev.platform_data;
        struct gpio_info_button  *buttons;
	struct evm_gpio_data     *gpdev;

	int  status;
	int  i=0;

	 if (!gpio_pdata) {
		
		printk("Platform Data Not found, gpio_driver could not be registered\n");
		return 0;
	}

	gpdev = kzalloc(sizeof(*gpdev), GFP_KERNEL);
	if (!gpdev)
		return -ENOMEM;
	
	buttons = kzalloc(gpio_pdata->nbuttons * (sizeof *buttons), GFP_KERNEL);
	if (!buttons)
		return -ENOMEM;	
	
	buttons = gpio_pdata->buttons;

	gpdev->devt = MKDEV(GPIO_DRIVER_MAJOR, GPIO_DRIVER_MINOR);

	status = register_chrdev_region (gpdev->devt, NUMBER_OF_DEVICE, "gpio_driver");
	
        if (status < 0) {
		return -EINVAL;
	}

	cdev_init (&gpdev->cdev, &gpio_fops);
	gpdev->cdev.owner = THIS_MODULE;
	gpdev->cdev.ops   = &gpio_fops;
	status            = cdev_add (&gpdev->cdev, gpdev->devt, NUMBER_OF_DEVICE);
	
        if(status < 0) {
	   goto free_chrdev_region;
	   printk("Driver Registration Failed....\n");
        }
	
	gpdev->class = class_create(THIS_MODULE, "gpio_driver");
        if (IS_ERR(gpdev->class)) {
                status = PTR_ERR(gpdev->class);
                goto unregister_chrdev;
        }

        gpdev->dev = device_create(gpdev->class, NULL, gpdev->devt, NULL, "gpio_driver");
        
	if (IS_ERR(gpdev->dev)) {
                status = PTR_ERR(gpdev->dev);
                goto unregister_chrdev;
        }

	  spin_lock(&gpdev->sp_lock);

	  for (i = 0; i < gpio_pdata->nbuttons; i++){

		      status = gpio_request(buttons[i].gpio, buttons[i].name);

                       if (status < 0) {
                              printk(KERN_ERR "Failed to request gpio_id->'%d' for gpio_driver\n", buttons[i].id);

              		      gpio_free(buttons[i].gpio);
                              goto unregister_chrdev;
	       	       }
        		 if (buttons[i].direction == OUTPUT){
		                if (buttons[i].default_level == LOW){
                                    gpio_direction_output(buttons[i].gpio, 0);

			        }else if (buttons[i].default_level == HIGH){
				    gpio_direction_output(buttons[i].gpio, 1);

				}else
				    gpio_direction_output(buttons[i].gpio, 0);
                                
			 }else if (buttons[i].direction == INPUT)
			     gpio_direction_input(buttons[i].gpio);
	  }	

	spin_unlock(&gpdev->sp_lock);
	gpdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, gpdev);	
	evm_gpio_data = pdev;
	
	printk("GPIO Driver Registration Successfully...\n");
	return status;

unregister_chrdev:
	cdev_del(&gpdev->cdev);
	platform_set_drvdata(pdev, NULL);
free_chrdev_region:
	unregister_chrdev_region(gpdev->devt, NUMBER_OF_DEVICE);
	platform_set_drvdata(pdev, NULL);
        return status;
}

/*-------------------------------------------------------------------------*/
/*             Exits GPIO_Driver		                           */
/*-------------------------------------------------------------------------*/
static int __devexit gpio_remove(struct platform_device *pdev)
{
	struct evm_gpio_data 		*rgpdev = platform_get_drvdata(pdev);
	struct gpio_info_platform_data	*rgpio_pdata = pdev->dev.platform_data;	
	struct gpio_info_button        	*rbuttons = rgpio_pdata->buttons;
	int				i;

	   for (i = 0; i < rgpio_pdata->nbuttons; i++)
		gpio_free(rbuttons[i].gpio);
	        
        cdev_del(&rgpdev->cdev);
	unregister_chrdev_region(rgpdev->devt, NUMBER_OF_DEVICE);
	device_destroy(rgpdev->class, rgpdev->devt);
	class_destroy(rgpdev->class);
	kfree(rgpdev);
	kfree(rgpio_pdata);
	kfree(rbuttons);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

/*-------------------------------------------------------------------------*/
/*     Platform Driver                                                     */
/*-------------------------------------------------------------------------*/
static struct platform_driver evm_gpio_driver = {
        .driver = {
                .name   = "gpio_driver",
                .owner  = THIS_MODULE,
        },
        .probe          = gpio_probe,
        .remove         = __devexit_p(gpio_remove),
};

/*-------------------------------------------------------------------------*/
/*     GPIO Driver Inititialization   		                           */
/*-------------------------------------------------------------------------*/
static int __init gpio_init(void)
{
   return platform_driver_register(&evm_gpio_driver);
}

/*-------------------------------------------------------------------------*/
/*      Exit the GPIO_Driver                                               */  
/*-------------------------------------------------------------------------*/
static void __exit gpio_exit(void)
{
   platform_driver_unregister(&evm_gpio_driver);
}

module_init(gpio_init);
module_exit(gpio_exit);

/*-------------------------------------------------------------------------*/
/*      Misc Init		                                           */
/*-------------------------------------------------------------------------*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jithin Shyam, <jithin.s@calixto.co.in>");
MODULE_DESCRIPTION("CALIXTO-EVM GPIO driver");
MODULE_ALIAS("platform:gpio-driver");
