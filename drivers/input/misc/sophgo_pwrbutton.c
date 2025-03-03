// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SOPHGO Reset button ACPI driver
 *
 * Copyright (C) 2025, Jingyu Li <jingyu.li01@sophgo.com>
 */

#define pr_fmt(fmt) "ACPI: SOPHGO reset button: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/acpi.h>
#include <linux/slab.h>

#define ACPI_SOPHGO_NAME		"SOPHGO Reset Button"
#define ACPI_SOPHGO_CLASS		"Reset"
#define ACPI_SOPHGO_NOTIFY_STATUS	0x80

struct sophgo_reset_button {
	struct input_dev *input;
	char phys[32];
	unsigned long pushed;
	bool suspended;
};

static void sophgo_reset_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = data;
	struct sophgo_reset_button *button = acpi_driver_data(device);
	struct input_dev *input;

	if (event != ACPI_SOPHGO_NOTIFY_STATUS) {
		pr_debug("Unsupported event [0x%x]\n", event);
		return;
	}

	acpi_pm_wakeup_event(&device->dev);

	if (button->suspended)
		return;

	input = button->input;

	input_report_key(input, KEY_RESTART, 1);
	input_sync(input);
	input_report_key(input, KEY_RESTART, 0);
	input_sync(input);

	acpi_bus_generate_netlink_event(device->pnp.device_class,
			dev_name(&device->dev), event, ++button->pushed);
}

#ifdef CONFIG_PM_SLEEP
static int sophgo_reset_suspend(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct sophgo_reset_button *button = acpi_driver_data(device);

	button->suspended = true;
	return 0;
}

static int sophgo_reset_resume(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct sophgo_reset_button *button = acpi_driver_data(device);

	button->suspended = false;
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sophgo_reset_pm,
		sophgo_reset_suspend, sophgo_reset_resume);

static int sophgo_reset_add(struct acpi_device *device)
{
	struct sophgo_reset_button *button;
	struct input_dev *input;
	acpi_status status;
	int error;

	button = kzalloc(sizeof(struct sophgo_reset_button), GFP_KERNEL);
	if (!button) {
		pr_err("Failed to allocate memory for button\n");
		return -ENOMEM;
	}

	device->driver_data = button;

	button->input = input = input_allocate_device();
	if (!input) {
		pr_err("Failed to allocate memory for input device\n");
		error = -ENOMEM;
		goto err_free_button;
	}

	snprintf(button->phys, sizeof(button->phys),
			"%s/reset/input0", acpi_device_hid(device));

	input->name = "SOPHGO Reset Button";
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->dev.parent = &device->dev;

	input_set_capability(input, EV_KEY, KEY_RESTART);

	error = input_register_device(input);
	if (error) {
		pr_err("Failed to register input device\n");
		goto err_free_input;
	}

	status = acpi_install_notify_handler(device->handle,
			ACPI_DEVICE_NOTIFY,
			sophgo_reset_notify,
			device);
	if (ACPI_FAILURE(status)) {
		pr_err("Failed to install notify handler\n");
		error = -ENODEV;
		goto err_unregister_input;
	}

	device_init_wakeup(&device->dev, true);

	pr_info("Device [%s] initialized\n", acpi_device_bid(device));
	return 0;

err_unregister_input:
	input_unregister_device(input);
err_free_input:
	input_free_device(input);
err_free_button:
	kfree(button);
	return error;
}

static void sophgo_reset_remove(struct acpi_device *device)
{
	struct sophgo_reset_button *button = acpi_driver_data(device);

	acpi_remove_notify_handler(device->handle,
			ACPI_DEVICE_NOTIFY,
			sophgo_reset_notify);
	input_unregister_device(button->input);
	kfree(button);
}

static const struct acpi_device_id sophgo_reset_ids[] = {
	{ "SOPH0013", 0 }, /* SOPHGO Reset Button HID */
	{ },
};
MODULE_DEVICE_TABLE(acpi, sophgo_reset_ids);

static struct acpi_driver sophgo_reset_driver = {
	.name = ACPI_SOPHGO_NAME,
	.class = ACPI_SOPHGO_CLASS,
	.ids = sophgo_reset_ids,
	.ops = {
		.add = sophgo_reset_add,
		.remove = sophgo_reset_remove,
	},
	.drv.pm = &sophgo_reset_pm,
};

module_acpi_driver(sophgo_reset_driver);

MODULE_AUTHOR("Jingyu Li <jingyu.li01@sophgo.com>");
MODULE_DESCRIPTION("SOPHGO Reset Button ACPI Driver");
MODULE_LICENSE("GPL v2");
