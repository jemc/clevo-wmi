/*
 *  clevo_laptop.c - CLEVO laptop ACPI WMI extra driver
 *
 *  Copyright (C) 2009 Michael WJ Wang @ Clevo Co., TW
 *
 *  Thanks to all contributors for related codes, driver models and ideas..
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This kernel driver module is developed to support ACPI WMI hotkey events
 *  for Clevo laptops. In the moment (2009 A.D), Clevo laptops implement their
 *  keyboard hotkey events notification both via WMI/scancode. They may want to
 *  shift to support only WMI for all keyboard hotkey events. This can now be
 *  well handled for their M$-based laptops, but not for Linux-based. So, we
 *  need a way to get all available WMI hotkey events and then input to system
 *  or do something we want.
 *
 *  This kernel driver module DOES NOTHING FUNCTIONALLY to control your
 *  keyboard hotkeys YET. Because BIOS/EC still handle it almost correctly.
 *
 *  Expected capabilities:
 *      1. Get keyboard hotkey WMI event and export to sysfs. (Implemented)
 *      2. Map WMI event to keycode and input to system. (To be completed)
 *      3. LCD brightness control. (Implemented)
 *      4. Rfkill control. (To be implemented)
 *
 *  Bug reports, comments and successful experiences are all welcome.
 *  Please send your "dmidecode output" and "DSDT dump" w/ your message. =)
 *
 *  Project homepage:
 *              https://sourceforge.net/projects/clevo-extra/
 *
 *  History:
 *	2009-07-06 v1.0.0, -User space interface 
 *                          @ /sys/devices/platform/clevo_laptop/.
 *	2009-07-13 v1.1.1, -Add brightness control for testing.
 *			    (If generic driver is not available)
 *			   -Disable volume up/down keycode translating
 *			    temporarily due to a bug found.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/acpi.h>

#define CL_MODVER "1.1.1"

#define CL_PREFIX "clevo_laptop: "
#define CL_PERR KERN_ERR CL_PREFIX
#define CL_PWARN KERN_WARNING CL_PREFIX
#define CL_PINFO KERN_INFO CL_PREFIX

/*
#define CLACPI_SETHANDLE(object, paths...)        \
        static acpi_handle object##_handle = NULL;\
        static char *object##_paths[] = {paths}
 */

/* WMI event notification GUID
 * WMI control method (WMxx) GUID
 */
#define CLWMI_EVENT_GUID "ABBC0F6B-8EA1-11d1-00A0-C90629100000"
#define CLWMI_GET_GUID "ABBC0F6D-8EA1-11d1-00A0-C90629100000"

/* Notify(\_SB.WMI, 0xD0) by BIOS.
 * Currently only handle EC/KBC Fn+xx hotkey event.
 */
#define CLWMI_NOTIFY_HKEVENT 0xD0

/* Method ID for WMxx control method:
 *
 * (READ w/o "method_id->data")
 * 0x01 = keyboard hotkey event (currently actived)
 *
 *================================
 * (WRITE w/o "method_id->data")
 * 0x1B = setup E-mail LED OFF
 * 0x1C = setup E-mail LED ON
 *
 *================================
 * (WRITE w/ "method_id->data")
 * 0x27 = E-mail LED state
 * 	.data = 0, E-mail LED OFF
 *	.data = 1, E-mail LED ON
 */
#define CLWMI_METHODID_HKEVENT    0x01
#define CLWMI_METHODID_SETMLEDOFF 0x1B
#define CLWMI_METHODID_SETMLEDON  0x1C
#define CLWMI_METHODID_SETMLED    0x27

/* Keyboard hotkey event ID */
#define CLWMI_EVENTID_SILENTMODE 0xDE /* TODO: Higher trigger point of fan */
#define CLWMI_EVENTID_NORMALMODE 0xDF
#define CLWMI_EVENTID_BNLEVEL0   0xE0 /* Min LCD brightness level (NOT OFF) */
#define CLWMI_EVENTID_BNLMIN     CLWMI_EVENTID_BNLEVEL0
#define CLWMI_EVENTID_BNLEVEL1   0xE1
#define CLWMI_EVENTID_BNLEVEL2   0xE2
#define CLWMI_EVENTID_BNLEVEL3   0xE3
#define CLWMI_EVENTID_BNLEVEL4   0xE4
#define CLWMI_EVENTID_BNLEVEL5   0xE5
#define CLWMI_EVENTID_BNLEVEL6   0xE6
#define CLWMI_EVENTID_BNLEVEL7   0xE7 /* Max LCD brightness level */
#define CLWMI_EVENTID_BNLMAX     CLWMI_EVENTID_BNLEVEL7
#define CLWMI_EVENTID_3GOFF      0xEA
#define CLWMI_EVENTID_3GON       0xEB
#define CLWMI_EVENTID_WLANOFF    0xF4
#define CLWMI_EVENTID_WLANON     0xF5
#define CLWMI_EVENTID_CAMOFF     0xF6
#define CLWMI_EVENTID_CAMON      0xF7
#define CLWMI_EVENTID_BTOFF      0xF8
#define CLWMI_EVENTID_BTON       0xF9
#define CLWMI_EVENTID_VOLDOWN    0xFA /* TODO: How to judge down? */
#define CLWMI_EVENTID_VOLUP      0xFA /* TODO: How to judge up? */
#define CLWMI_EVENTID_MUTE       0xFB /* TODO: How to check on/off? */
#define CLWMI_EVENTID_TPOFF      0xFC
#define CLWMI_EVENTID_TPON       0xFD
#define CLWMI_EVENTID_BNLDOWN    0x10 /* Special for driver */
#define CLWMI_EVENTID_BNLUP      0x20 /* Special for driver */

/*===========================================================================
 * Following (#define)s are found in SiS/Intel platforms.
 * We use them in this driver for special purposes.
 *
 * DO NOT CHANGE, if you are not sure!
 */
#define CLACPI_EC_ADDR_BRIGHTNESS   0xC9
#define CLACPI_EC_CMD_SETBRIGHTNESS 0x8F /* wdata = 0~7   */
#define CLACPI_BCL_ITEMS            10   /* 2 + 8 (E0~E7) */
#define CLACPI_PATH_BQC "\\_SB.PCI0.AGP.VGA.LCD._BQC"
#define CLACPI_PATH_BCL "\\_SB.PCI0.AGP.VGA.LCD._BCL"
/* ECOS variable. EXPERIMENTAL USE ONLY.
 * 0x20 = Vista
 * 0x60 = Linux? XP? Others?
 */
#define CLACPI_PATH_ECOS  "\\_SB.PCI0.LPC.EC.ECOS"
#define CLACPI_PATH_ECOSI "\\_SB.PCI0.LPCB.EC.ECOS"
/* Brightness level index. EXPERIMENTAL USE ONLY.
 * OEM2 + 0xE0 == CLWMI_EVENTID_BNLEVEL[0~7]
 */
#define CLACPI_PATH_BNIDX  "\\_SB.PCI0.LPC.EC.OEM2"
#define CLACPI_PATH_BNIDXI "\\_SB.PCI0.LPCB.EC.OEM2"
/*===========================================================================
 */

static unsigned int __initdata force;
module_param_named(force, force, bool, 0);
MODULE_PARM_DESC(force, " Skip DMI detection");
static unsigned int __initdata brightness;
module_param_named(brightness, brightness, bool, 0);
MODULE_PARM_DESC(brightness, " Use this driver to control brightness");

MODULE_ALIAS("wmi:"CLWMI_EVENT_GUID);

static u32 clevo_wmi_lasthotkey = 0;
static u32 clevo_wmi_lastbnevent = 0;
static u32 clevo_wmi_curbnvalue;
static u32 clevo_wmi_bnelements[CLACPI_BCL_ITEMS];

/* Input arguments for control method */
struct method_args {
	const char *guid;
	u8 instance;
	u32 methodid;
	u32 data;
};

struct key_entry {
	char type;
	u32 eventid; /* Clevo spec */
	u16 keycode; /* include/linux/input.h */
};

enum {KE_KEY, KE_END};

/* WMI <-> keycode mapping table - generic */
static struct key_entry clevo_wmi_keymap[] = {
	{KE_KEY, CLWMI_EVENTID_BNLDOWN, KEY_BRIGHTNESSDOWN},
	{KE_KEY, CLWMI_EVENTID_BNLUP, KEY_BRIGHTNESSUP},
	{KE_KEY, CLWMI_EVENTID_3GOFF, KEY_WIMAX},     /* Keycode TBC */
	{KE_KEY, CLWMI_EVENTID_3GON, KEY_WIMAX},      /* Keycode TBC */
	{KE_KEY, CLWMI_EVENTID_WLANOFF, KEY_WLAN},
	{KE_KEY, CLWMI_EVENTID_WLANON, KEY_WLAN},
	{KE_KEY, CLWMI_EVENTID_CAMOFF, KEY_CAMERA},
	{KE_KEY, CLWMI_EVENTID_CAMON, KEY_CAMERA},
	{KE_KEY, CLWMI_EVENTID_BTOFF, KEY_BLUETOOTH},
	{KE_KEY, CLWMI_EVENTID_BTON, KEY_BLUETOOTH},
	//{KE_KEY, CLWMI_EVENTID_VOLDOWN, KEY_VOLUMEDOWN}, /* TBC */
	//{KE_KEY, CLWMI_EVENTID_VOLUP, KEY_VOLUMEUP},     /* TBC */
	{KE_KEY, CLWMI_EVENTID_MUTE, KEY_MUTE},
	//{KE_KEY, CLWMI_EVENTID_TPOFF, BTN_TOUCH},     /* Keycode TBC */
	//{KE_KEY, CLWMI_EVENTID_TPON, BTN_TOUCH},      /* Keycode TBC */
	{KE_END, 0}
};

static struct dmi_system_id __initdata cl_boardlist[] = {
        {
                .ident = "Clevo M74xS",
                .matches = {
                        DMI_MATCH(DMI_BOARD_VENDOR, "clevo"),
                        DMI_MATCH(DMI_BOARD_NAME, "M7x0S")
                }
        },
        {
                .ident = "Clevo W760T/M740T/M760T",
                .matches = {
                        DMI_MATCH(DMI_BOARD_VENDOR, "CLEVO Co."),
                        DMI_MATCH(DMI_BOARD_NAME, "W760T/M740T/M760T")
                }
        },
        {
                .ident = "Clevo M740TU(N)/M760TU(N)/W7X0TUN",
                .matches = {
                        DMI_MATCH(DMI_BOARD_VENDOR, "CLEVO Co."),
                        DMI_MATCH(DMI_BOARD_NAME, "M740TU(N)/M760TU(N)/W7X0TUN")
                }
        },
        {}
};

/* Tool:
 * Call a specific ACPI object in BIOS. Usually a method.
 * In this case it will return an u32 integer value.
 */
static acpi_status clevo_acpiobj_eval_int(char *path, u32 *out)
{
        unsigned long long value = 0;
        acpi_status status = AE_OK;

        if (!path || !out)
                return AE_BAD_PARAMETER;

        status = acpi_evaluate_integer(NULL, path, NULL, &value);

        if (ACPI_SUCCESS(status))
                *out = (u32)value;
        else
                printk(CL_PERR "Error evaluating %s\n", path);

        return status;
}

/* Tool:
 * Call a specific ACPI object in BIOS. Usually a method.
 * In this case it will return a package w/ elements pointer.
 */
static acpi_status clevo_acpiobj_eval_pkg(char *path, u32 *out_count, \
                                          u32 *out_list)
{
        struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
        union acpi_object *pkg;
        union acpi_object *element;
        u32 tmp_out[CLACPI_BCL_ITEMS];
        acpi_status status = AE_OK;
        u32 i;

        if (!path || !out_count || !out_list)
                return AE_BAD_PARAMETER;

        status = acpi_evaluate_object(NULL, path, NULL, &output);

        if (ACPI_SUCCESS(status)) {
                pkg = (union acpi_object *)output.pointer;
                *out_count = pkg->package.count; /* Elements count */
                for (i = 0; i < pkg->package.count; i++) {
                        element = &(pkg->package.elements[i]);
                        tmp_out[i] = element->integer.value;
                }
        }
        else {
                printk(CL_PERR "Error evaluating %s\n", path);
                return status;
        }

        for (i = 0; i < pkg->package.count; i++)
                out_list[i] = tmp_out[i];

        kfree(output.pointer);
        return status;
}


/* Backlight device declaration */
struct backlight_device *clevo_laptop_backlight_device;

static int clevo_bd_get_brightness(struct backlight_device *bd)
{
        u8 ret_index;

	ec_read(CLACPI_EC_ADDR_BRIGHTNESS, &ret_index);
        //clevo_acpiobj_eval_int(CLACPI_PATH_BQC, &ret_value);
        return (int)ret_index;
}

static int clevo_bd_set_brightness(struct backlight_device *bd)
{
	u8 wdata = bd->props.brightness;
	u8 rdata;

	if (wdata < 0 || wdata > (CLACPI_BCL_ITEMS - 2))
		return -EINVAL;
	ec_transaction(CLACPI_EC_CMD_SETBRIGHTNESS, &wdata, sizeof(wdata), \
		       &rdata, sizeof(rdata), 1);

        return 0;
}

struct backlight_ops clevo_bd_ops = {
        .update_status = clevo_bd_set_brightness,
        .get_brightness = clevo_bd_get_brightness,
};


/* Platform device declaration */
static struct platform_device *clevo_laptop_platform_dev;

static acpi_status __init clevo_laptop_get_init_bnlevel(void)
{
        u32 bnlevels_count;
        u32 bnlevels_elements[CLACPI_BCL_ITEMS];
        u32 cur_bnvalue, i;
        acpi_status status;

        status = clevo_acpiobj_eval_int(CLACPI_PATH_BQC, &cur_bnvalue);
        if (ACPI_FAILURE(status))
                return status;
        /* Initial value of brightness */
        clevo_wmi_curbnvalue = cur_bnvalue;
        //printk(CL_PINFO "Current brightness: %d\n", clevo_wmi_curbnvalue);

        status = clevo_acpiobj_eval_pkg(CLACPI_PATH_BCL, &bnlevels_count, \
                                        &bnlevels_elements);
        if (ACPI_FAILURE(status))
                return status;
        /* Store all _BCL brightness values to somewhere else */
        for (i = 0; i < bnlevels_count; i++)
                clevo_wmi_bnelements[i] = bnlevels_elements[i];

        /* We don't use first 2 elements */
        for (i = 2; i < bnlevels_count; i++) {
                if (cur_bnvalue == clevo_wmi_bnelements[i])
                /* This is what we want... */
                        clevo_wmi_lastbnevent = \
                                              CLWMI_EVENTID_BNLEVEL0 + (i - 2);
        }

        return AE_OK;
}

static ssize_t show_last_hotkey(struct device *dev, \
			       struct device_attribute *attr, char *buf)
{
	u32 eventid;

	eventid = clevo_wmi_lasthotkey;
	if (eventid < 0)
		return -EINVAL;
	return sprintf(buf, "%X\n", eventid);
}

static ssize_t show_current_brightness(struct device *dev, \
                               struct device_attribute *attr, char *buf)
{
        u32 value;

        clevo_acpiobj_eval_int(CLACPI_PATH_BQC, &value);

        if (value < 0)
                return -EINVAL;

        clevo_wmi_curbnvalue = value;
        return sprintf(buf, "%d\n", value);
}

static DEVICE_ATTR(last_hotkey, 0444, show_last_hotkey, NULL);
static DEVICE_ATTR(current_brightness, 0444, show_current_brightness, NULL);

static struct key_entry *clevo_wmi_get_entry_by_eventid(u32 eventid)
{
	struct key_entry *ke;

	for (ke = clevo_wmi_keymap; ke->type != KE_END; ke++)
		if (ke->eventid == eventid && ke->type == KE_KEY)
			return ke;
	return NULL;
}

static struct key_entry *clevo_wmi_get_entry_by_keycode(u16 keycode)
{
	struct key_entry *ke;

	for (ke = clevo_wmi_keymap; ke->type != KE_END; ke++)
		if (ke->keycode == keycode && ke->type == KE_KEY)
			return ke;
	return NULL;
}

static struct input_dev *clevo_wmi_input_dev;

static int clevo_wmi_getkeycode(struct input_dev *dev, \
				       int eventid, int *keycode)
{
	struct key_entry *ke;

	ke = clevo_wmi_get_entry_by_eventid(eventid);
	if (ke && ke->type == KE_KEY) {
		*keycode = ke->keycode;
		return 0;
	}
	return -EINVAL;
}

static int clevo_wmi_setkeycode(struct input_dev *dev, \
				       int eventid, int keycode)
{
	struct key_entry *ke;
	u16 tmpkeycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;
	ke = clevo_wmi_get_entry_by_eventid((u32)eventid);
	if (ke->type == KE_KEY && ke) {
		tmpkeycode = (u16)ke->keycode;
		ke->keycode = (u16)keycode;
		set_bit(keycode, dev->keybit);
		if (!clevo_wmi_get_entry_by_keycode((u16)tmpkeycode))
			clear_bit(tmpkeycode, dev->keybit);
		return 0;
	}
	return -EINVAL;
}

/* To judge it's up/down for brightness event. */
static int clevo_wmi_check_bnevent(u32 in, u32 *out)
{
        switch (in) {
        case CLWMI_EVENTID_BNLMAX:
                *out = CLWMI_EVENTID_BNLUP;
                clevo_wmi_lastbnevent = CLWMI_EVENTID_BNLMAX;
                break;
        case CLWMI_EVENTID_BNLMIN:
                *out = CLWMI_EVENTID_BNLDOWN;
                clevo_wmi_lastbnevent = CLWMI_EVENTID_BNLMIN;
                break;
        default: /* 0xE1 <= in <= 0xE6 */
                if (clevo_wmi_lastbnevent > in)
                        *out = CLWMI_EVENTID_BNLDOWN;
                else
                        *out = CLWMI_EVENTID_BNLUP;

                clevo_wmi_lastbnevent = in;
                break;
        }
        return 0;
}

/* Execute WMI control method. */
static acpi_status clevo_wmi_exec_method(struct method_args *in, u32 *out)
{
	acpi_status status;
	u32 extradata = in->data; /* Reserved for extensibility */
	struct acpi_buffer input = {sizeof(u32), &extradata};
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	u32 tmp = 0;

	status = wmi_evaluate_method(in->guid, in->instance, in->methodid, \
				     &input, &output);

	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *)output.pointer;

	if (obj && obj->type == ACPI_TYPE_INTEGER)
		tmp = (u32)obj->integer.value;

	if (out)
		*out = tmp;

	kfree(output.pointer);
	return AE_OK;
}

/* WMI notify handler. (\_SB.WMI) */
static void clevo_wmi_notify_handler(u32 value, void *context)
{
	acpi_status status = 0;
	struct method_args args;
	u32 eventid = 0, findevent = 0;
	static struct key_entry *ke;

	switch (value) {
	case CLWMI_NOTIFY_HKEVENT:
		args.guid = CLWMI_GET_GUID;
		args.instance = 1;
		args.methodid = CLWMI_METHODID_HKEVENT;
		args.data = 0;
		break;
	default:
		printk(CL_PERR "Unknown notify 0x%X received", value);
		break;
	}

	/* Get event ID. */
	status = clevo_wmi_exec_method(&args, &eventid);

	if (ACPI_FAILURE(status))
		printk(CL_PERR "Execution of the control method failed\n"\
			       "              Bad data for WMxx method\n"\
			       "              Error status: 0x%X\n", status);
	else {
		clevo_wmi_lasthotkey = eventid;
		//printk(CL_PINFO "== WMI event ID: 0x%X ==\n", \
		//			clevo_wmi_lasthotkey);

                /* Do a pre-process for brightness WMI events.
                 * Because BIOS returned eventid is "level" not "up/down".
                 */
                if (eventid <= CLWMI_EVENTID_BNLEVEL7 && \
                    eventid >= CLWMI_EVENTID_BNLEVEL0)
                        clevo_wmi_check_bnevent(eventid, &findevent);
                else
                        findevent = eventid;

                //printk(CL_PINFO "findevent: 0x%X\n", findevent);

                /* Get keycode via keymap. */
		ke = clevo_wmi_get_entry_by_eventid(findevent);
		if (ke) {
			switch (ke->type) {
			case KE_KEY:
				input_report_key(clevo_wmi_input_dev, \
						 ke->keycode, 1);
				input_sync(clevo_wmi_input_dev);
				input_report_key(clevo_wmi_input_dev, \
						 ke->keycode, 0);
				input_sync(clevo_wmi_input_dev);
				break;
			default:
				break;
			}
		}
	}
}

static int __init clevo_wmi_input_dev_init(void)
{
	struct key_entry *ke;
	int error;

	clevo_wmi_input_dev = input_allocate_device();
	if (!clevo_wmi_input_dev)
		return -ENOMEM;

	clevo_wmi_input_dev->name = "Clevo WMI keyboard hotkeys";
	clevo_wmi_input_dev->phys = "wmi/input0";
	clevo_wmi_input_dev->id.bustype = BUS_HOST;
	clevo_wmi_input_dev->setkeycode = clevo_wmi_setkeycode;
	clevo_wmi_input_dev->getkeycode = clevo_wmi_getkeycode;

	for (ke = clevo_wmi_keymap; ke->type != KE_END; ke++) {
		switch (ke->type) {
		case KE_KEY:
			set_bit(EV_KEY, clevo_wmi_input_dev->evbit);
			set_bit(ke->keycode, clevo_wmi_input_dev->keybit);
			break;
                default:
                        break;
		}
	}

	error = input_register_device(clevo_wmi_input_dev);
	if (!error)
		return 0;
	input_free_device(clevo_wmi_input_dev);
	return error;
}

static int __init clevo_laptop_bd_init(void)
{
        struct backlight_device *bd;

        bd = backlight_device_register("clevo_laptop", NULL, NULL, &clevo_bd_ops);
        if (IS_ERR(bd)) {
                printk(CL_PERR "Fail registering Clevo backlight device\n");
                backlight_device_unregister(clevo_laptop_backlight_device);
                return (PTR_ERR(bd));
        }
        clevo_laptop_backlight_device = bd;

        bd->props.max_brightness = (CLACPI_BCL_ITEMS - 3);
        bd->props.brightness = (CLACPI_BCL_ITEMS - 3);
        backlight_update_status(bd);

        return 0;
}

static void __exit clevo_laptop_bd_exit(void)
{
        backlight_device_unregister(clevo_laptop_backlight_device);
}

static int __init clevo_laptop_platform_init(struct platform_device *device)
{
	int error = 0;

	error = device_create_file(&device->dev, &dev_attr_last_hotkey);
	if (error)
		goto platform_init_err;
	error = device_create_file(&device->dev, &dev_attr_current_brightness);
	if (error)
		goto platform_init_err;

	return 0;

platform_init_err:
	device_remove_file(&device->dev, &dev_attr_last_hotkey);
        device_remove_file(&device->dev, &dev_attr_current_brightness);
	return error;
}

static int __exit clevo_laptop_platform_remove(struct platform_device *device)
{
	device_remove_file(&device->dev, &dev_attr_last_hotkey);
        device_remove_file(&device->dev, &dev_attr_current_brightness);
	return 0;
}

static struct platform_driver clevo_laptop_driver = {
	.driver = {
		.name = "clevo_laptop",
		.owner = THIS_MODULE,
	},
	.probe = clevo_laptop_platform_init,
	.remove = clevo_laptop_platform_remove,
};


static int __init clevo_laptop_init(void)
{
	u32 status = -ENOMEM;

        if (acpi_disabled)
                return -ENODEV;

        if (!force) {
                if (!dmi_check_system(cl_boardlist)) {
                        printk(CL_PERR "Not Clevo board, unable to load\n");
			return -ENODEV;
                }
        }

        if (!wmi_has_guid(CLWMI_EVENT_GUID) || !wmi_has_guid(CLWMI_GET_GUID)) {
                printk(CL_PERR "No WMI GUIDs, unable to load\n");
		return -ENODEV;
        }

	status = wmi_install_notify_handler(CLWMI_EVENT_GUID, \
		   clevo_wmi_notify_handler, NULL);
	if (ACPI_FAILURE(status)) {
		printk(CL_PERR "Register WMI notify handler failed, "\
			       "error status: 0x%X\n", status);
		return -EINVAL;
	}
	printk(CL_PINFO "WMI notify handler registered\n");

        status = clevo_laptop_get_init_bnlevel();
        if (ACPI_FAILURE(status)) {
                printk(CL_PWARN "Init brightness status fail. Use default.\n");
                clevo_wmi_curbnvalue = 0x64; /* TODO: No good... */
        }
        if (brightness) {
                if (acpi_video_backlight_support())
                        printk(CL_PWARN "Generic ACPI video backlight support"\
                               " available, we will use it\n");
                else
                        clevo_laptop_bd_init();
        }

	status = clevo_wmi_input_dev_init();
	if (status) {
		printk(CL_PERR "Register WMI input device failed\n");
		goto fail_out1;
	}

	status = platform_driver_register(&clevo_laptop_driver);
	if (status) {
		printk(CL_PERR "Register platform driver failed\n");
		goto fail_out2;
	}
	clevo_laptop_platform_dev = platform_device_alloc("clevo_laptop", -1);
	if (!clevo_laptop_platform_dev) {
		printk(CL_PERR "Allocate platform device failed\n");
		goto fail_out3;
	}
	platform_device_add(clevo_laptop_platform_dev);

        printk(CL_PINFO "++ Kernel module loaded ++\n");
        return 0;

fail_out3:
	platform_driver_unregister(&clevo_laptop_driver);
fail_out2:
	input_unregister_device(clevo_wmi_input_dev);
fail_out1:
	wmi_remove_notify_handler(CLWMI_EVENT_GUID);

	printk(CL_PERR "!! Kernel module not loaded !!\n");
	return status;
}

static void __exit clevo_laptop_exit(void)
{

	platform_device_del(clevo_laptop_platform_dev);
	platform_driver_unregister(&clevo_laptop_driver);
        clevo_laptop_bd_exit();
	input_unregister_device(clevo_wmi_input_dev);
	wmi_remove_notify_handler(CLWMI_EVENT_GUID);

	printk(CL_PINFO "-- Kernel module unloaded --\n");
}

MODULE_VERSION(CL_MODVER);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CLEVO laptop ACPI WMI extra driver");
MODULE_AUTHOR("Michael WJ Wang <weijoe.wang AT gmail.com>");

module_init(clevo_laptop_init);
module_exit(clevo_laptop_exit);

