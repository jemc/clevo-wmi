/*
 * tuxedo-wmi.c
 *
 * Copyright (C) 2013  Christoph Jaeger <christophjaeger@linux.com>
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the  GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is  distributed in the hope that it  will be useful, but
 * WITHOUT  ANY   WARRANTY;  without   even  the  implied   warranty  of
 * MERCHANTABILITY  or FITNESS FOR  A PARTICULAR  PURPOSE.  See  the GNU
 * General Public License for more details.
 *
 * You should  have received  a copy of  the GNU General  Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#define TUXEDO_DRIVER_NAME KBUILD_MODNAME

/* \_SB_.WMI_.WMBB */
#define TUXEDO_WMBB_METHOD_GUID  "ABBC0F6D-8EA1-11D1-00A0-C90629100000"
#define TUXEDO_WMI_EVENT_D0_GUID "ABBC0F6B-8EA1-11D1-00A0-C90629100000"
#define TUXEDO_WMI_EVENT_D1_GUID "ABBC0F6C-8EA1-11D1-00A0-C90629100000" /* ??? */


enum kb_color {
	KB_COLOR_BLACK, /* means OFF */
	KB_COLOR_BLUE,
	KB_COLOR_RED,
	KB_COLOR_MAGENTA,
	KB_COLOR_GREEN,
	KB_COLOR_CYAN,
	KB_COLOR_YELLOW,
	KB_COLOR_WHITE,
};

const char *const kb_color_names[] = {
	"black",
	"blue",
	"red",
	"magenta",
	"green",
	"cyan",
	"yellow",
	"white",
};

#define KB_COLOR_DEFAULT      KB_COLOR_BLUE
#define KB_BRIGHTNESS_MAX     10
#define KB_BRIGHTNESS_DEFAULT KB_BRIGHTNESS_MAX

static int param_set_kb_color(const char *val, const struct kernel_param *kp)
{
	size_t i;

	if (!val)
		return -EINVAL;

	if (!val[0]) {
		*((enum kb_color *) kp->arg) = KB_COLOR_BLACK;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(kb_color_names); i++) {
		if (!strcmp(val, kb_color_names[i])) {
			*((enum kb_color *) kp->arg) = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int param_get_kb_color(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%s", kb_color_names[*((enum kb_color *) kp->arg)]);
}

static const struct kernel_param_ops param_ops_kb_color = {
	.set = param_set_kb_color,
	.get = param_get_kb_color,
};

static enum kb_color param_kb_color[3] = { [0 ... 2] = KB_COLOR_DEFAULT };
static int param_kb_color_num = 3;
#define param_check_kb_color(name, p) __param_check(name, p, enum kb_color)
module_param_array_named(kb_color, param_kb_color, kb_color,
                         &param_kb_color_num, S_IRUSR);
MODULE_PARM_DESC(kb_color, "Set the color(s) of the keyboard (sections)");


static int param_set_kb_brightness(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_byte(val, kp);

	if (!ret && *((unsigned char *) kp->arg) > KB_BRIGHTNESS_MAX)
		return -EINVAL;

	return ret;
}

static int param_get_kb_brightness(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%hhu", *((unsigned char *) kp->arg));
}

static const struct kernel_param_ops param_ops_kb_brightness = {
	.set = param_set_kb_brightness,
	.get = param_get_kb_brightness,
};

static unsigned char param_kb_brightness = KB_BRIGHTNESS_DEFAULT;
#define param_check_kb_brightness param_check_byte
module_param_named(kb_brightness, param_kb_brightness, kb_brightness, S_IRUSR);
MODULE_PARM_DESC(kb_brightness, "Set the brightness of the keyboard backlight");


static bool param_kb_off = false;
module_param_named(kb_off, param_kb_off, bool, S_IRUSR);
MODULE_PARM_DESC(kb_off, "Switch keyboard backlight off");


#define TUXEDO_DRIVER_NAME KBUILD_MODNAME
#define POLL_FREQ_MIN     1
#define POLL_FREQ_MAX     20
#define POLL_FREQ_DEFAULT 5

static int param_set_poll_freq(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_byte(val, kp);

	if (!ret)
		clamp_t(unsigned char, *((unsigned char *) kp->arg),
		        POLL_FREQ_MIN, POLL_FREQ_MAX);

	return ret;
}

static int param_get_poll_freq(char *buffer, const struct kernel_param *kp)
{
	/* due to a bug in the kernel we do this our own */
	return sprintf(buffer, "%hhu", *((unsigned char *) kp->arg));
}

static const struct kernel_param_ops param_ops_poll_freq = {
	.set = param_set_poll_freq,
	.get = param_get_poll_freq,
};

static unsigned char param_poll_freq = POLL_FREQ_DEFAULT;
#define param_check_poll_freq param_check_byte
module_param_named(poll_freq, param_poll_freq, poll_freq, S_IRUSR);
MODULE_PARM_DESC(poll_freq, "Set polling frequency");


static bool has_kb_backlight = false;

static int __init tuxedo_dmi_check_p15sm_p17sm_cb(const struct dmi_system_id *id)
{
	has_kb_backlight = true;
	return 1;
}

static struct dmi_system_id __initdata tuxedo_dmi_table[] = {
	{
		.ident = "Clevo P15xSM",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Notebook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P15SM"),
		},
		.callback = tuxedo_dmi_check_p15sm_p17sm_cb,
	},
	{
		.ident = "Clevo P17xSM",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Notebook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P17SM"),
		},
		.callback = tuxedo_dmi_check_p15sm_p17sm_cb,
	},
	{
		/* terminating NULL entry */
	},
};

MODULE_DEVICE_TABLE(dmi, tuxedo_dmi_table);


struct platform_device *tuxedo_platform_device;

/* input sub-driver */

static struct input_dev *tuxedo_input_device;
static DEFINE_MUTEX(tuxedo_input_report_mutex);

static unsigned int global_report_cnt = 0;

/* call with tuxedo_input_report_mutex held */
static void tuxedo_input_report_key(unsigned int code)
{
	input_report_key(tuxedo_input_device, code, 1);
	input_report_key(tuxedo_input_device, code, 0);
	input_sync(tuxedo_input_device);

	global_report_cnt++;
}

static struct task_struct *tuxedo_input_polling_task;

static int tuxedo_input_polling_thread(void *data)
{
	unsigned int report_cnt = 0;

	pr_info(TUXEDO_DRIVER_NAME ": Polling thread started (PID: %i), "
		"polling at %i Hz\n", current->pid, param_poll_freq);

	while (!kthread_should_stop()) {

		u8 byte;

		ec_read(0xDB, &byte);
		if (byte & 0x40) {
			ec_write(0xDB, byte & ~0x40);

			pr_info(TUXEDO_DRIVER_NAME ": polld: Hotkey pressed\n");

			mutex_lock(&tuxedo_input_report_mutex);

			if (global_report_cnt > report_cnt) {
				mutex_unlock(&tuxedo_input_report_mutex);
				break;
			}

			tuxedo_input_report_key(KEY_RFKILL);
			report_cnt++;

			mutex_unlock(&tuxedo_input_report_mutex);
		}
		msleep(1000 / param_poll_freq);
	}

	pr_info(TUXEDO_DRIVER_NAME ": polld: Bye!\n");

	return 0;
}

static int tuxedo_input_open(struct input_dev *dev)
{
	tuxedo_input_polling_task = kthread_run(tuxedo_input_polling_thread,
	                                       NULL, "tuxedo-polld");

	if (unlikely(IS_ERR(tuxedo_input_polling_task))) {
		tuxedo_input_polling_task = NULL;
		pr_err(TUXEDO_DRIVER_NAME ": Could not create polling thread\n");
                return -EBUSY; // PTR_RET
	}

        return 0;
}

static void tuxedo_input_close(struct input_dev *dev)
{
	if (unlikely(IS_ERR_OR_NULL(tuxedo_input_polling_task)))
		return;

	kthread_stop(tuxedo_input_polling_task);
	tuxedo_input_polling_task = NULL;
}

static int __init tuxedo_input_init(void)
{
	int err;
	u8 byte;

	tuxedo_input_device = input_allocate_device();
        if (unlikely(!tuxedo_input_device)) {
                pr_err(TUXEDO_DRIVER_NAME ": Error allocating input device\n");
                return -ENOMEM;
        }

        tuxedo_input_device->name = "Clevo Airplane-Mode Hotkey";
        tuxedo_input_device->phys = TUXEDO_DRIVER_NAME "/input0";
        tuxedo_input_device->id.bustype = BUS_HOST;
	tuxedo_input_device->dev.parent = &tuxedo_platform_device->dev;

	tuxedo_input_device->open  = tuxedo_input_open;
	tuxedo_input_device->close = tuxedo_input_close;

	set_bit(EV_KEY, tuxedo_input_device->evbit);
	set_bit(KEY_RFKILL, tuxedo_input_device->keybit);

	ec_read(0xDB, &byte);
	ec_write(0xDB, byte & ~0x40);

	err = input_register_device(tuxedo_input_device);
        if (unlikely(err)) {
                pr_err(TUXEDO_DRIVER_NAME ": Error registering input device\n");
                goto err_free_input_device;
        }

	return 0;

err_free_input_device:
        input_free_device(tuxedo_input_device);

        return err;
}

static void tuxedo_input_exit(void)
{
	if (unlikely(!tuxedo_input_device))
		return;

	input_unregister_device(tuxedo_input_device);
        tuxedo_input_device = NULL;
}


static int tuxedo_wmi_evaluate_wmbb_method(u32 method_id, u32 arg, u32 *retval)
{
	struct acpi_buffer in  = { (acpi_size) sizeof(arg), &arg };
        struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
        union acpi_object *obj;
        acpi_status status;
	u32 tmp;

	status = wmi_evaluate_method(TUXEDO_WMBB_METHOD_GUID, 0x01,
	                             method_id, &in, &out);

        if (ACPI_FAILURE(status))
		goto exit;

        obj = (union acpi_object *) out.pointer;
        if (obj && obj->type == ACPI_TYPE_INTEGER)
                tmp = (u32) obj->integer.value;
        else
                tmp = 0;

        if (retval)
                *retval = tmp;

        kfree(obj);

exit:
        if (ACPI_FAILURE(status))
                return -EIO;

	return 0;
}

union kb_command {
	u32 raw;
	struct {
		u32 left       :4,
		    center     :4,
		    right      :4,
		    brightness :4,
		    local3     :8,
		    local4     :4,
		    local7     :4;
	};
} kb_cmd;

enum kb_effect {
	KB_EFFECT_CUSTOM,
	KB_EFFECT_CYCLE,
	KB_EFFECT_BREATHING = 0x3,
	KB_EFFECT_RANDOM_COLOR = 0x7,
	KB_EFFECT_DANCING_EFFECT,
	KB_EFFECT_TEMPO_BEAT,
	KB_EFFECT_FLASHING,
	KB_EFFECT_UPDOWN_WAVE,
};

#define KB_OFF    0x2
#define kb_is_off (kb_cmd.local7 == KB_OFF)

static void kb_dec_brightness(void)
{
	union kb_command tmp;

	if (kb_is_off || kb_cmd.local7 != KB_EFFECT_CUSTOM)
		return;
	if (kb_cmd.brightness <= 0)
		return;

	tmp = kb_cmd;
	tmp.brightness--;
	tmp.local7 = 0xD;
	if (!tuxedo_wmi_evaluate_wmbb_method(0x67, tmp.raw, NULL))
		kb_cmd.brightness--;
}

static void kb_inc_brightness(void)
{
	union kb_command tmp;

	if (kb_is_off || kb_cmd.local7 != KB_EFFECT_CUSTOM)
		return;
	if (kb_cmd.brightness >= KB_BRIGHTNESS_MAX)
		return;

	tmp = kb_cmd;
	tmp.brightness++;
	tmp.local7 = 0xD;
	if (!tuxedo_wmi_evaluate_wmbb_method(0x67, tmp.raw, NULL))
		kb_cmd.brightness++;
}

static void kb_next_effect(void)
{
	static enum kb_effect effects[] = {
		KB_EFFECT_RANDOM_COLOR,
		KB_EFFECT_DANCING_EFFECT,
		KB_EFFECT_TEMPO_BEAT,
		KB_EFFECT_FLASHING,
		KB_EFFECT_UPDOWN_WAVE,
		KB_EFFECT_BREATHING,
		KB_EFFECT_CYCLE,
		KB_EFFECT_CUSTOM,
	};

	static size_t i = 0; /* holds index of next effect */

	union kb_command tmp;

	if (kb_is_off)
		return;

	tmp = kb_cmd;
	tmp.local7 = KB_OFF;
	if (!tuxedo_wmi_evaluate_wmbb_method(0x67, tmp.raw, NULL)) {
		tmp.local7 = effects[i];
		if (!tuxedo_wmi_evaluate_wmbb_method(0x67, tmp.raw, NULL)) {
			kb_cmd = tmp;
			i = (i + 1) % ARRAY_SIZE(effects);
		}
	}
}

static void kb_toggle(void)
{
	static union kb_command save;

	if (kb_is_off) {
		if (!tuxedo_wmi_evaluate_wmbb_method(0x67, save.raw, NULL))
			kb_cmd = save;
	} else {
		union kb_command tmp = save = kb_cmd;

		tmp.local7 = KB_OFF;
		if (!tuxedo_wmi_evaluate_wmbb_method(0x67, tmp.raw, NULL))
			kb_cmd = tmp;
	}
}

static void tuxedo_wmi_notify(u32 value, void *context)
{
	static unsigned int report_cnt = 0;

	u32 event;

	if (value != 0xD0) {
		pr_notice(TUXEDO_DRIVER_NAME ": Unexpected WMI event (%0#4x)\n",
		          value);
		return;
	}

	tuxedo_wmi_evaluate_wmbb_method(0x01, 0, &event);

	switch (event) {
	case 0xF4:
		pr_info(TUXEDO_DRIVER_NAME ": Hotkey pressed\n");

		if (tuxedo_input_polling_task) {
			pr_info(TUXEDO_DRIVER_NAME ": Stopping polld\n");
			kthread_stop(tuxedo_input_polling_task);
			tuxedo_input_polling_task = NULL;
		}

		mutex_lock(&tuxedo_input_report_mutex);

		if (global_report_cnt > report_cnt) {
			mutex_unlock(&tuxedo_input_report_mutex);
			break;
		}

		tuxedo_input_report_key(KEY_RFKILL);
		report_cnt++;

		mutex_unlock(&tuxedo_input_report_mutex);
		break;
	default:
		if (!has_kb_backlight)
			break;

		switch (event) {
		case 0x81:
			kb_dec_brightness();
			break;
		case 0x82:
			kb_inc_brightness();
			break;
		case 0x83:
			kb_next_effect();
			break;
		case 0x9F:
			kb_toggle();
			break;
		}
		break;
	}
}

static int tuxedo_wmi_probe(struct platform_device *dev)
{
	int status;

	status = wmi_install_notify_handler(TUXEDO_WMI_EVENT_D0_GUID,
	                                    tuxedo_wmi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err(TUXEDO_DRIVER_NAME ": Could not register WMI "
		       "notify handler (%0#X)\n", status);
		return -EIO;
	}

	tuxedo_wmi_evaluate_wmbb_method(0x46, 0, NULL);

	if (has_kb_backlight) {

		kb_cmd.left   = param_kb_color[0];
		kb_cmd.center = param_kb_color[1];
		kb_cmd.right  = param_kb_color[2];
		kb_cmd.brightness = param_kb_brightness;
		kb_cmd.local3 = 1;
		kb_cmd.local4 = 2;
		kb_cmd.local7 = KB_EFFECT_CUSTOM;
		kb_toggle();

		if (!param_kb_off)
			kb_toggle();
	}

	return 0;
}

static int tuxedo_wmi_remove(struct platform_device *dev)
{
	wmi_remove_notify_handler(TUXEDO_WMI_EVENT_D0_GUID);
	return 0;
}

static int tuxedo_wmi_resume(struct platform_device *dev)
{
	tuxedo_wmi_evaluate_wmbb_method(0x46, 0, NULL);
	if (has_kb_backlight)
		tuxedo_wmi_evaluate_wmbb_method(0x67, kb_cmd.raw, NULL);
	return 0;
}

static struct platform_driver tuxedo_platform_driver = {
	.remove = tuxedo_wmi_remove,
	.resume = tuxedo_wmi_resume,
	.driver = {
		.name  = TUXEDO_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};


/* LED sub-driver */

static struct workqueue_struct *led_workqueue;

static struct _led_work {
	struct work_struct work;
	int wk;
} led_work;

static void airplane_led_update(struct work_struct *work)
{
	u8 byte;
	struct _led_work *w;

	w = container_of(work, struct _led_work, work);

	ec_read(0xD9, &byte);
	ec_write(0xD9, w->wk ? byte | 0x40 : byte & ~0x40);
}

static enum led_brightness airplane_led_get(struct led_classdev *led_cdev)
{
	u8 byte;

	ec_read(0xD9, &byte);
	return byte & 0x40 ? LED_FULL : LED_OFF;
}

/* must not sleep */
static void airplane_led_set(struct led_classdev *led_cdev,
                             enum led_brightness value)
{
	led_work.wk = value;
	queue_work(led_workqueue, &led_work.work);
}

static struct led_classdev airplane_led = {
	.name = "tuxedo::airplane",
	.brightness_get = airplane_led_get,
	.brightness_set = airplane_led_set,
	.max_brightness = 1,
};

static int __init tuxedo_led_init(void)
{
	int err;

	led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (unlikely(!led_workqueue))
		return -ENOMEM;

	INIT_WORK(&led_work.work, airplane_led_update);

	err = led_classdev_register(&tuxedo_platform_device->dev, &airplane_led);
	if (err)
		goto err_destroy_workqueue;

	return 0;

err_destroy_workqueue:
	destroy_workqueue(led_workqueue);
	led_workqueue = NULL;

	return err;
}

static void tuxedo_led_exit(void)
{
	if (!IS_ERR_OR_NULL(airplane_led.dev))
		led_classdev_unregister(&airplane_led);
	if (led_workqueue)
		destroy_workqueue(led_workqueue);
}


static int __init tuxedo_init(void)
{
	int err;

	switch (param_kb_color_num) {
	case 1:
		param_kb_color[1] = param_kb_color[2] = param_kb_color[0];
		break;
	case 2:
		return -EINVAL;
	}

	dmi_check_system(tuxedo_dmi_table);

	if (!wmi_has_guid(TUXEDO_WMI_EVENT_D0_GUID)) {
		printk(TUXEDO_DRIVER_NAME ": No known WMI "
		       "event notification GUID found\n");
		return -ENODEV;
	}

	if (!wmi_has_guid(TUXEDO_WMBB_METHOD_GUID)) {
		printk(TUXEDO_DRIVER_NAME ": No known WMI "
		       "control method GUID found\n");
		return -ENODEV;
	}

	tuxedo_platform_device =
		platform_create_bundle(&tuxedo_platform_driver,
		                       tuxedo_wmi_probe, NULL, 0, NULL, 0);

	if (unlikely(IS_ERR(tuxedo_platform_device)))
		return PTR_RET(tuxedo_platform_device);

	err = tuxedo_input_init();
	if (unlikely(err))
		pr_err(TUXEDO_DRIVER_NAME ": Could not register input device\n");

	err = tuxedo_led_init();
	if (unlikely(err))
		pr_err(TUXEDO_DRIVER_NAME ": Could not register LED device\n");

	return 0;
}

static void __exit tuxedo_exit(void)
{
	tuxedo_led_exit();
	tuxedo_input_exit();

	platform_device_unregister(tuxedo_platform_device);
	platform_driver_unregister(&tuxedo_platform_driver);
}

module_init(tuxedo_init);
module_exit(tuxedo_exit);

MODULE_AUTHOR("Christoph Jaeger <christophjaeger@linux.com>");
MODULE_DESCRIPTION("Clevo laptop driver.");
MODULE_LICENSE("GPL");
