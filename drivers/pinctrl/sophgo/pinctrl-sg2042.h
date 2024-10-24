/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _PINCTRL_SOPHGO_SG2042_H
#define _PINCTRL_SOPHGO_SG2042_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>

#define PIN_FLAG_DEFAULT			0
#define PIN_FLAG_WRITE_HIGH			BIT(0)
#define PIN_FLAG_ONLY_ONE_PULL			BIT(1)
#define PIN_FLAG_NO_PINMUX			BIT(2)
#define PIN_FLAG_NO_OEX_EN			BIT(3)
#define PIN_FLAG_IS_ETH				BIT(4)

struct sg2042_pin {
	u16				pin;
	u16				offset;
	u16				flags;
};

struct sg2042_pinctrl_data {
	const struct pinctrl_pin_desc		*pins;
	const struct sg2042_pin			*pindata;
	const u32				*oc_reg_map;
	u32					pullup_res;
	u32					pulldown_res;
	u16					npins;
	u16					noc_reg;
};

int sg2042_pinctrl_probe(struct platform_device *pdev);

#define SG2042_GENERAL_PIN(_id,	_offset, _flag)				\
	{								\
		.pin = (_id),						\
		.offset = (_offset),					\
		.flags = (_flag),					\
	}

#endif
