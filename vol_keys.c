#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/spmi.h>
#include <soc/qcom/smsm.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/workqueue.h>


#define DEVICE_NAME               "vol_keys"
#define SWITCH_DEV_NAME           "soc:hiby,sa_sound_switch"
#define SPMI_DEV_NAME             "qpnp-vadc-15"
#define BUFFER_SIZE               3
#define MEDIA_BUTTON_ID           1
#define MEDIA_BUTTON_V_MIN        160
#define MEDIA_BUTTON_V_MAX        170
#define VOLUME_UP_BUTTON_ID       2
#define VOLUME_UP_BUTTON_V_MIN    190
#define VOLUME_UP_BUTTON_V_MAX    205
#define VOLUME_DOWN_BUTTON_ID     3
#define VOLUME_DOWN_BUTTON_V_MIN  225
#define VOLUME_DOWN_BUTTON_V_MAX  250
#define SLEEP_TIME_MSEC           100


static void vol_wq_handler(struct work_struct *w);
struct workqueue_struct *wq = 0;
DECLARE_DELAYED_WORK(vol_wq_work, vol_wq_handler);
unsigned long delay;

//static DEFINE_MUTEX(sensor_value_mtx);

struct input_dev *input_dev;

extern struct bus_type *spmi_bus_type;
struct qpnp_vadc_chip *vadc_chip;
struct qpnp_vadc_result vadc_result;

unsigned int spmi_buf[BUFFER_SIZE] = {0};
unsigned int spmi_res;

uint32_t smsm_state;

struct timespec start_time;
struct timespec current_time;
unsigned int time_diff = 0;

unsigned int headset_gpio, balance_gpio;

int debug = 0;
module_param(debug, int, S_IRUGO);

const unsigned short keyspan_key_table[] = {
    KEY_VOLUMEUP,   // 115
    KEY_VOLUMEDOWN, // 114
    KEY_PLAYPAUSE,  // 164
};


void print_datetime(void)
{
    struct timeval now;
    struct tm tm_val;

    do_gettimeofday(&now);
    time_to_tm(now.tv_sec, 0, &tm_val);
    pr_info("datetime: %d/%d/%d %02d:%02d:%02d\n",
            tm_val.tm_mday, tm_val.tm_mon + 1, (int)tm_val.tm_year + 1900,
            tm_val.tm_hour + 4, tm_val.tm_min, tm_val.tm_sec);
}

static int input_device_register(void)
{
    int i, err;
    char physName[32];

    /* Allocate an input device data structure */
    input_dev = input_allocate_device();
    if (!input_dev) {
	pr_err("%s: error in input_alloc_device()\n", DEVICE_NAME);
        return -EINVAL;
    }

    input_dev->name = DEVICE_NAME;
    snprintf(physName, sizeof(physName), "%s/input0", DEVICE_NAME);
    input_dev->phys = physName;

    input_dev->id.bustype = BUS_HOST;
    input_dev->id.vendor = 0x0001;
    input_dev->id.product = 0x0002;
    input_dev->id.version = 0x0100;

    __set_bit(EV_KEY, input_dev->evbit);
    for (i = 0; i < ARRAY_SIZE(keyspan_key_table); i++)
        __set_bit(keyspan_key_table[i], input_dev->keybit);

    /* Register with the input subsystem */
    err = input_register_device(input_dev);
    if (err) {
	pr_err("%s: unable to register input device: %d\n", DEVICE_NAME, err);
	return err;
    }

    pr_info("%s: input device initialized\n", DEVICE_NAME);
    return 0;
}

static void vol_wq_handler(struct work_struct *w)
{
    int i, ret = 0;
    bool connected = true, match = true, first_press = false;

    //mutex_lock(&sensor_value_mtx);

    /* Check if headset is connected and balance not */
    if (headset_gpio && balance_gpio) {
        if (!gpio_get_value(headset_gpio) || gpio_get_value(balance_gpio)) {
            connected = false;
            goto done;
        }
    }

    /* Read ADC value from mpp1_div1 */
    ret = qpnp_vadc_read(vadc_chip, P_MUX1_1_1, &vadc_result);
    if (ret) {
        pr_err("%s: error in qpnp_vadc_read()\n", DEVICE_NAME);
        goto done;
    }

    spmi_res = vadc_result.physical / 1000;

    /* Get current time in seconds */
    get_monotonic_boottime(&current_time);

    /* Count difference since last cycle */
    time_diff = (int)(current_time.tv_sec - start_time.tv_sec);

    /* Shift elements in buffer to left */
    for (i = 0; i < BUFFER_SIZE-1; i++)
        spmi_buf[i] = spmi_buf[i+1];

    /* Fill last element of the array with defined button ID or 0 */
    if (spmi_res >= MEDIA_BUTTON_V_MIN && spmi_res <= MEDIA_BUTTON_V_MAX) {
        spmi_buf[BUFFER_SIZE-1] = MEDIA_BUTTON_ID;
    } else if (spmi_res >= VOLUME_UP_BUTTON_V_MIN && spmi_res <= VOLUME_UP_BUTTON_V_MAX) {
        spmi_buf[BUFFER_SIZE-1] = VOLUME_UP_BUTTON_ID;
    } else if (spmi_res >= VOLUME_DOWN_BUTTON_V_MIN && spmi_res <= VOLUME_DOWN_BUTTON_V_MAX) {
        spmi_buf[BUFFER_SIZE-1] = VOLUME_DOWN_BUTTON_ID;
    } else {
        spmi_buf[BUFFER_SIZE-1] = 0;
    }

    /* Check matching of values in buffer except first value */
    for (i = 1; i < BUFFER_SIZE-1; i++) {
        if (spmi_buf[i] != spmi_buf[i+1] && (spmi_buf[i] != VOLUME_UP_BUTTON_ID || spmi_buf[i] != VOLUME_DOWN_BUTTON_ID)) {
            match = false;
            break;
        }
    }

    /* Get deivce power state */
    smsm_state = smsm_get_state(SMSM_APPS_STATE);

    /* Print device state, spmi result, IDs from buffer and loop time in debug mode */
    if (debug) {
        print_datetime();
        //pr_info("SMSM state: %x\n", smsm_state);
        pr_info("device state: %s\n", smsm_state < SMSM_PROC_AWAKE ? "suspend" : "active");
        pr_info("loop time: %d sec\n", time_diff);
        pr_info("spmi res: %d\n", spmi_res);
        pr_cont("%s data: [", DEVICE_NAME);
        for (i = 0; i < BUFFER_SIZE-1; i++)
            pr_cont("%d, ", spmi_buf[i]);
        pr_cont("%d]\n", spmi_buf[BUFFER_SIZE-1]);
    }

    /* Send keycodes if matching succeeded */
    if (match) {
        switch (spmi_buf[BUFFER_SIZE-1])
        {
            case VOLUME_UP_BUTTON_ID:
                input_report_key(input_dev, KEY_VOLUMEUP, 1);
                pr_info("[HS KEY] key report: %d state:1\n", KEY_VOLUMEUP);
                input_report_key(input_dev, KEY_VOLUMEUP, 0);
                pr_info("[HS KEY] key report: %d state:0\n", KEY_VOLUMEUP);
                if (spmi_buf[0] != VOLUME_UP_BUTTON_ID)
                    first_press = true;
                break;
            case VOLUME_DOWN_BUTTON_ID:
                input_report_key(input_dev, KEY_VOLUMEDOWN, 1);
                pr_info("[HS KEY] key report: %d state:1\n", KEY_VOLUMEDOWN);
                input_report_key(input_dev, KEY_VOLUMEDOWN, 0);
                pr_info("[HS KEY] key report: %d state:0\n", KEY_VOLUMEDOWN);
                if (spmi_buf[0] != VOLUME_DOWN_BUTTON_ID)
                    first_press = true;
                break;
            default:
                {}
        }
    }

    /* If media key holds while sleeping send key to start playing */
    if ((smsm_state < SMSM_PROC_AWAKE || time_diff > 1) && spmi_buf[BUFFER_SIZE-1] == MEDIA_BUTTON_ID && spmi_buf[0] != MEDIA_BUTTON_ID) {
        input_report_key(input_dev, KEY_PLAYPAUSE, 1);
        pr_info("[HS KEY] key report: %d state:1\n", KEY_PLAYPAUSE);
        input_report_key(input_dev, KEY_PLAYPAUSE, 0);
        pr_info("[HS KEY] key report: %d state:0\n", KEY_PLAYPAUSE);
    }

    //mutex_unlock(&sensor_value_mtx);
    input_sync(input_dev);
    get_monotonic_boottime(&start_time);

done:
    if (!connected) {
        delay = msecs_to_jiffies(10000);
    } else if (ret) {
        delay = msecs_to_jiffies(1000);
    } else if (first_press) {
        delay = msecs_to_jiffies(500);
    } else {
        delay = msecs_to_jiffies(SLEEP_TIME_MSEC);
    }

    if (wq)
        queue_delayed_work(wq, &vol_wq_work, delay);
}

static int vol_keys_init(void)
{
    int err;
    u32 reg[2];
    struct device *switch_dev;
    struct device_node *nd;
    struct device *spmi_dev;

    if (BUFFER_SIZE < 1) {
        pr_err("%s: wrong size of buffer detected\n", DEVICE_NAME);
        return -EINVAL;
    }

    spmi_dev = bus_find_device_by_name(&spmi_bus_type, NULL, SPMI_DEV_NAME);
    if (!spmi_dev) {
        pr_err("%s: can't find %s device\n", DEVICE_NAME, SPMI_DEV_NAME);
        return -EINVAL;
    }

    vadc_chip = dev_get_drvdata(spmi_dev);
    if (IS_ERR(vadc_chip) || vadc_chip == NULL) {
        pr_err("%s: get chip fail, err = %d", DEVICE_NAME, vadc_chip);
        return -EINVAL;
    }

    err = input_device_register();
    if (err < 0) {
        pr_err("%s: error in input_device_register(): %d\n", DEVICE_NAME, err);
        return -EINVAL;
    }

    switch_dev = bus_find_device_by_name(&platform_bus_type, NULL, SWITCH_DEV_NAME);
    if (!switch_dev) {
        pr_err("%s: could not find switch device: %s\n", DEVICE_NAME, SWITCH_DEV_NAME);
    } else {
        nd = of_find_node_by_name(switch_dev->of_node, "headset");
        if (nd) {
            err = of_property_read_u32_array(nd, "gpios", reg, 2);
            if (!err) {
                headset_gpio = (int)reg[1];
                pr_info("%s: headset gpio: %d\n", DEVICE_NAME, headset_gpio);
            }
        }
        nd = of_find_node_by_name(switch_dev->of_node, "balance");
        if (nd) {
            err = of_property_read_u32_array(nd, "gpios", reg, 2);
            if (!err) {
                balance_gpio = (int)reg[1];
                pr_info("%s: balance gpio: %d\n", DEVICE_NAME, balance_gpio);
            }
        }
    }

    if (!wq)
        wq = create_singlethread_workqueue("vol_keys_workqueue");
    if (wq)
        queue_delayed_work(wq, &vol_wq_work, delay);

    delay = msecs_to_jiffies(SLEEP_TIME_MSEC);
    get_monotonic_boottime(&start_time);

    pr_info("%s: workqueue loaded: %u jiffies\n", DEVICE_NAME, (unsigned)delay);
    return 0;
}

static void vol_keys_exit(void)
{
    input_unregister_device(input_dev);
    pr_info("%s: input device unregistered\n", DEVICE_NAME);

    cancel_work_sync(&vol_wq_work);

    if (wq)
        destroy_workqueue(wq);

    pr_info("%s: workqueue exit\n", DEVICE_NAME);
}

module_init(vol_keys_init);
module_exit(vol_keys_exit);
MODULE_AUTHOR("Snoopy112 <snoopy112@yandex.ru>");
MODULE_DESCRIPTION("HiBy volume keys headset driver");
MODULE_LICENSE("GPL");