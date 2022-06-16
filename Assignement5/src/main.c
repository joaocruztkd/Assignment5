/*
 * Paulo Pedreiras, 2022/02
 * Zephyr: Simple thread and digital IO example
 * 
 * Reads a button and sets a led according to the button state
 *
 * Base documentation:
 *  Zephyr kernel: 
 *      https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/zephyr/reference/kernel/index.html
 *      
 *  DeviceTree 
 *      Board DTS can be found in BUILD_DIR/zephyr/zephyr.dts
 *      https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/zephyr/guides/dts/api-usage.html#dt-from-c  
 *
 *  HW info
 *      https://infocenter.nordicsemi.com/topic/struct_nrf52/struct/nrf52840.html
 *      Section: nRF52840 Product Specification -> Peripherals -> GPIO / GPIOTE
 * 
 *      Board specific HW info can be found in the nRF52840_DK_User_Guide_20201203. I/O pins available at pg 27
 * 
 * 
 */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>
#include <timing/timing.h>
#include <stdio.h>

static struct gpio_callback but1_cb_data; /* Callback structure */
/* Size of stack area used by each thread (can be thread specific, if necessary)*/
#define STACK_SIZE 1024

/* Thread scheduling priority */
#define thread_manual_prio 1
#define thread_relogio_prio 1

/* Therad periodicity (in ms)*/
#define thread_relogio_period 1000


/* Create thread stack space */
K_THREAD_STACK_DEFINE(thread_manual_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_relogio_stack, STACK_SIZE);
  
/* Create variables for thread data */
struct k_thread thread_manual_data;
struct k_thread thread_relogio_data;

/* Create task IDs */
k_tid_t thread_manual_tid;
k_tid_t thread_relogio_tid;

/** Global vars (shared memory between tasks A/B and B/C, resp) */
int seg = 0;
int min = 0;
int horas = 0;

/** Semaphores for task synch */
struct k_sem sem_manual;

/* Thread code prototypes */
void thread_manual_code(void *argA, void *argB, void *argC);
void thread_relogio_code(void *argA, void *argB, void *argC);

/* Refer to dts file */
#define GPIO0_NID DT_NODELABEL(gpio0) 
#define BOARDLED_PIN 0xe /* Pin at which LED is connected. Addressing is direct (i.e., pin number) */
#define BOARDBUT1 0xb /* Pin at which BUT1 is connected. Addressing is direct (i.e., pin number) */
#define PWM0_NID DT_NODELABEL(pwm0)

/* Main function */
void main(void) {

    /** Create and init semaphores */
    k_sem_init(&sem_manual, 0, 1);

     

    thread_manual_tid = k_thread_create(&thread_manual_data, thread_manual_stack,
        K_THREAD_STACK_SIZEOF(thread_manual_stack), thread_manual_code,
        NULL, NULL, NULL, thread_manual_prio, 0, K_NO_WAIT);
    thread_relogio_tid = k_thread_create(&thread_relogio_data, thread_relogio_stack,
        K_THREAD_STACK_SIZEOF(thread_relogio_stack), thread_relogio_code,
        NULL, NULL, NULL, thread_relogio_prio, 0, K_NO_WAIT);


    return;

} 

/* Thread code implementation */
void thread_manual_code(void *argA , void *argB, void *argC)
{
    /* Local vars */
    
    int ret=0;                              /* Generic return value variable */
    
    /* Task init code */
    printk("Thread A init (periodic)\n");
   
    /* Callback function and variables*/
    volatile int dcToggleFlag = 0; /* Flag to signal a BUT1 press */

    void but1press_cbfunction(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
    
    /* Inform that button was hit*/
    printk("But1 pressed at %d\n\r", k_cycle_get_32());
    
    /* Update Flag*/
    dcToggleFlag = 1;
}

  /* Local vars */
    const struct device *gpio0_dev;         /* Pointer to GPIO device structure */
    const struct device *pwm0_dev;          /* Pointer to PWM device structure */
      
    unsigned int pwmPeriod_us = 1000;       /* PWM priod in us */
    unsigned int dcValue[]={0,33,66,100};   /* Duty-cycle in % */
    unsigned int dcIndex=0;                 /* DC Index */
    
    /* Task init code */
    printk("pwmDemo\n\r"); 
    printk("Hit But1 to cycle among intensities ...\n\r ");

    /* Bind to GPIO 0 and PWM0 */
    gpio0_dev = device_get_binding(DT_LABEL(GPIO0_NID));
    if (gpio0_dev == NULL) {
        printk("Error: Failed to bind to GPIO0\n\r");        
	return;
    }
    else {
        printk("Bind to GPIO0 successfull \n\r");        
    }
    
    pwm0_dev = device_get_binding(DT_LABEL(PWM0_NID));
    if (pwm0_dev == NULL) {
	printk("Error: Failed to bind to PWM0\n r");
	return;
    }
    else  {
        printk("Bind to PWM0 successful\n\r");            
    }

    
    /* Configure PINS */    
    
    /* Note that PCB does not include pull-up resistors */
    /* See nRF52840v1.0.0 DK Users Guide V 1.0.0, pg 29 */
    ret = gpio_pin_configure(gpio0_dev, BOARDBUT1, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) {
        printk("Error %d: Failed to configure BUT 1 \n\r", ret);
	return;
    }

    /* Set interrupt HW - which pin and event generate interrupt */
    ret = gpio_pin_interrupt_configure(gpio0_dev, BOARDBUT1, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
	printk("Error %d: failed to configure interrupt on BUT1 pin \n\r", ret);
	return;
    }
    
    /* Set callback */
    gpio_init_callback(&but1_cb_data, but1press_cbfunction, BIT(BOARDBUT1));
    gpio_add_callback(gpio0_dev, &but1_cb_data);
    
    
    /* main loop */
    while(1) {    
    
    k_sem_take(&sem_manual,  K_FOREVER);    
               
        if(dcToggleFlag) {
            dcIndex++;
            if(dcIndex == 4) 
                dcIndex = 0;
            dcToggleFlag = 0;
            printk("PWM DC value set to %u %%\n\r",dcValue[dcIndex]);

            ret = pwm_pin_set_usec(pwm0_dev, BOARDLED_PIN,
		      pwmPeriod_us,(unsigned int)((pwmPeriod_us*dcValue[dcIndex])/100), PWM_POLARITY_NORMAL);
            if (ret) {
                printk("Error %d: failed to set pulse width\n", ret);
		return;
            }
        }            
           
    }

    return;
    k_sem_give(&sem_manual);
}
        
    

   


/* Thread code implementation */
void thread_relogio_code(void *argA , void *argB, void *argC)
{
    /* Local vars */
    int64_t fin_time=0, release_time=0;     /* Timing variables to control task periodicity */
    
    int ret=0;                              /* Generic return value variable */
    
    /* Task init code */
    printk("Thread Relogio init (periodic)\n");
           
    /* Compute next release instant */
    release_time = k_uptime_get() + thread_relogio_period;

    /* Thread loop */
    while(1) {        
        
        printk("Thread Relogio activated\n\r");  
        
        seg=seg+1;

        if(seg < 60)
          seg=seg+1;
        else
          seg=0;
          min=min+1;

        if(min > 59)
          min=0;
          horas=horas + 1;
        
        if(horas >24)
          horas=0;    
              
        /* Wait for next release instant */ 
        fin_time = k_uptime_get();
        if( fin_time < release_time) {
            k_msleep(release_time - fin_time);
            release_time += thread_A_period;

        }
    }
}
