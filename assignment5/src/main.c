/** @file main.c
 * @brief Program that variates the intensity of a LED
 *
 * In this program, there is thread A which is periodic, and the other two
 * are sporadic and activated via semaphores. The data is communicated 
 * via shared memory. 
 * Every 1s, it's taken one sample from the ADC module. Then, it's calculated 
 * the average between 10 samples and the outliers are discarded from the average value.
 * Afterwards, the PWM DC is set to the dutty-cycle that corresponds to the average value.
 * That is going to control the LED_1 intensity.
 *
 * @author Ana Luísa Coelho, 93371
 *	   Soraia Souto, 93308
 *	   João Cruz, 92930
 * @date 28 of May 2022
 * @bug Not detected! 
 */


#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/pwm.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>
#include <timing/timing.h>
#include <stdlib.h>
#include <stdio.h>


/** Number of samples for the average*/
#define dados_size 10

/** Size of stack area used by each thread (can be thread specific, if necessary)*/
#define STACK_SIZE 1024

/** Thread scheduling priority */
#define thread_A_prio 1
#define thread_B_prio 1
#define thread_C_prio 1

/** Therad periodicity (in ms)*/
#define SAMP_PERIOD_MS 1000       /** Set to have a period of 1s*/  

/** ADC definitions and includes*/
#include <hal/nrf_saadc.h>
#define ADC_NID DT_NODELABEL(adc) 
#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_4
#define ADC_REFERENCE ADC_REF_VDD_1_4
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)
#define ADC_CHANNEL_ID 1  

#define ADC_CHANNEL_INPUT NRF_SAADC_INPUT_AIN1          /** Analog 1 - Port P0.03 */

#define BUFFER_SIZE 1

/** Refer to dts file */
#define GPIO0_NID DT_NODELABEL(gpio0) 
#define PWM0_NID DT_NODELABEL(pwm0) 
#define BOARDLED1 0x0d                                  /** LED 1 */
                

/** Create thread stack space */
K_THREAD_STACK_DEFINE(thread_A_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_B_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_C_stack, STACK_SIZE);
  
/** Create variables for thread data */
struct k_thread thread_A_data;
struct k_thread thread_B_data;
struct k_thread thread_C_data;

/** Create task IDs */
k_tid_t thread_A_tid;
k_tid_t thread_B_tid;
k_tid_t thread_C_tid;

/** Global vars (shared memory between tasks A/B and B/C, resp) */
int ab = 0;
int bc = 0;

/** Semaphores for task synch */
struct k_sem sem_ab;
struct k_sem sem_bc;

/** Thread code prototypes */
void thread_A_code(void *argA, void *argB, void *argC);
void thread_B_code(void *argA, void *argB, void *argC);
void thread_C_code(void *argA, void *argB, void *argC);

/** ADC channel configuration */
static const struct adc_channel_cfg my_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_CHANNEL_ID,
	.input_positive = ADC_CHANNEL_INPUT
};

/** Global vars */
struct k_timer my_timer;
const struct device *adc_dev = NULL;
static uint16_t adc_sample_buffer[BUFFER_SIZE];

/** Takes one sample */
static int adc_sample(void)
{
	int ret;
	const struct adc_sequence sequence = {
		.channels = BIT(ADC_CHANNEL_ID),
		.buffer = adc_sample_buffer,
		.buffer_size = sizeof(adc_sample_buffer),
		.resolution = ADC_RESOLUTION,
	};

	if (adc_dev == NULL) {
            printk("adc_sample(): error, must bind to adc first \n\r");
            return -1;
	}

	ret = adc_read(adc_dev, &sequence);
	if (ret) {
            printk("adc_read() failed with code %d\n", ret);
	}	

	return ret;
}

/** Main function */
void main(void) {
    
    /** Welcome message */
    printf("\n\r Illustration of the use of shmem + semaphores\n\r");
    
    /** Create and init semaphores */
    k_sem_init(&sem_ab, 0, 1);
    k_sem_init(&sem_bc, 0, 1);
    
    /** Create tasks */
    thread_A_tid = k_thread_create(&thread_A_data, thread_A_stack,
        K_THREAD_STACK_SIZEOF(thread_A_stack), thread_A_code,
        NULL, NULL, NULL, thread_A_prio, 0, K_NO_WAIT);

    thread_B_tid = k_thread_create(&thread_B_data, thread_B_stack,
        K_THREAD_STACK_SIZEOF(thread_B_stack), thread_B_code,
        NULL, NULL, NULL, thread_B_prio, 0, K_NO_WAIT);

    thread_B_tid = k_thread_create(&thread_C_data, thread_C_stack,
        K_THREAD_STACK_SIZEOF(thread_C_stack), thread_C_code,
        NULL, NULL, NULL, thread_C_prio, 0, K_NO_WAIT);
    
    return;
} 


/** 
* ADC module Thread (periodic):
*
* Gets one sample per second from Analog 1 and it
* sets the ab to the value of that sample. This will 
* be processed in the next thread. 
*
*/
void thread_A_code(void *argA , void *argB, void *argC)
{
    /** Timing variables to control task periodicity */
    int64_t fin_time=0, release_time=0;

    int err=0;
    
    printk("Thread A init (periodic)\n");

    /** Compute next release instant */
    release_time = k_uptime_get() + SAMP_PERIOD_MS;

    /** ADC setup: bind and initialize */
    adc_dev = device_get_binding(DT_LABEL(ADC_NID));
    if (!adc_dev) {
        printk("ADC device_get_binding() failed\n");
    } 
    err = adc_channel_setup(adc_dev, &my_channel_cfg);
    if (err) {
        printk("adc_channel_setup() failed with error code %d\n", err);
    }
    
    /** Calibration of the SAADC*/
    NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;

    while(1) {
        
        /** Gets one sample and checks for errors*/
        err=adc_sample();
        if(err) {
            printk("adc_sample() failed with error code %d\n\r",err);
        }
        else {
            if(adc_sample_buffer[0] > 1023) {
                printk("adc reading out of range\n\r");
            }
            else {
                /** ADC is set to use gain of 1/4 and reference VDD/4, so input range is [0...VDD_3V], with 10 bit resolution */
                /** Global variable ab will assume values between [0, 1023]*/
                ab=adc_sample_buffer[0];
            }
        }

        printk("Thread A set ab value to: %d \n",ab);  
        
        k_sem_give(&sem_ab);
       
        /** Waits for next release instant */ 
        fin_time = k_uptime_get();
        if( fin_time < release_time) {        
            k_msleep(release_time - fin_time);            
            release_time += SAMP_PERIOD_MS;
        }
    }
}

/** 
* Processement Thread:
*
* In this thread, there's a local array named dados, which holds the samples 
* collected in the last thread (A). This array is rotated clockwise to be update 
* every single time it gets a new sample.
* There's a cycle that calculates the first average and prevents in the beginning
* that the zeros mess up the average value. This is followed by another cycle that
* calculates a second average value, discarding the values that are 10% above or bellow 
* the first average value.
* In the end, it sets variable bc to the average value.
*
*/
void thread_B_code(void *argA , void *argB, void *argC)
{    
    /** Creation of the array that will hold the samples from the ADC module*/
    int dados[dados_size]={0};

    printk("Thread B init (sporadic, waits on a semaphore by task A)\n");
    while(1) {

        int sum1=0, sum2=0, cnt1=0, cnt2=0, Avg=0;
        int i=0, j=0, l=0, x=0;

        k_sem_take(&sem_ab,  K_FOREVER);
        
        printk("Task B read ab value: %d\n",ab);

        dados[0] = ab;

        /** Rotates the array clockwise*/                             
        x = dados[dados_size-1];                /** Stores the last element of the array*/           
        for(l = (dados_size-1); l > 0; l--){                    
            dados[l] = dados[l-1];   
        }               
        dados[0] = x;                           /** Last element of the array will be added to the start of the array*/ 
                                          
        /** Prevents the initial zeros from messing up the average*/
        for(i = 0; i < dados_size; i++){
            if(dados[i] != 0){
                sum1 = sum1 + dados[i];
                cnt1++; 
            }
            else
                sum1=sum1;
        }
       
        /** To avoid sum1 dividing by 0*/
        if(cnt1 != 0)
            Avg=sum1/cnt1;
        else 
            Avg = 0;

        /*printk("sum1: %d, cnt1: %d, Avg1: %d\n", sum1, cnt1, Avg);*/

        /** Choose the values that are not acording to the average*/        
        for(j = 0; j < dados_size; j++){
            if(dados[j] < (Avg - Avg*0.1) || dados[j] > (Avg + Avg*0.1))
                sum2=sum2;
            else{
                sum2 = sum2 + dados[j];
                cnt2++;
            }             
        }
        
        /** To avoid sum2 dividing by 0*/
        if(cnt2 != 0)
            Avg=sum2/cnt2;
        else 
            Avg = 0;

        /*printk("sum2: %d, cnt2: %d, Avg2: %d\n", sum2, cnt2, Avg);*/

        bc=Avg;

        printk("Thread B set bc value to: %d \n",bc);  
        k_sem_give(&sem_bc);        
  }
}

/** 
* Output Thread:
*
* PWM DC is set to the dutty-cycle that is associated 
* to the average value in bc. This will 
* allow the control of the intensity of LED_1. 
*
*/
void thread_C_code(void *argA , void *argB, void *argC)
{
    const struct device *gpio0_dev;         /** Pointer to GPIO device structure */
    const struct device *pwm0_dev;          /** Pointer to PWM device structure */
    int ret=0;                              /** Generic return value variable */
    
    unsigned int pwmPeriod_us = 1000;       /** PWM period in us */

    printk("Thread C init (sporadic, waits on a semaphore by task B)\n");
    
    /** Bind to GPIO 0 and PWM0 */
    gpio0_dev = device_get_binding(DT_LABEL(GPIO0_NID));
    if (gpio0_dev == NULL) {
        printk("Error: Failed to bind to GPIO0\n\r");        
	return;
    }
        
    pwm0_dev = device_get_binding(DT_LABEL(PWM0_NID));
    if (pwm0_dev == NULL) {
	printk("Error: Failed to bind to PWM0\n r");
	return;
    }
   
    while(1) {
        k_sem_take(&sem_bc, K_FOREVER);
        ret=0;

        /** Sets the PWM DC value to the average of the samples got from ADC module in thread A*/
        ret = pwm_pin_set_usec(pwm0_dev, BOARDLED1,
		      pwmPeriod_us,(unsigned int)((pwmPeriod_us*bc)/1023), PWM_POLARITY_NORMAL);
        if (ret) {
            printk("Error %d: failed to set pulse width\n", ret);
            return;
        }
                       
        printk("Task C - PWM: %u \n", ((pwmPeriod_us*bc)/10023));   /** Prints dutty-cycle*/
    }
}
