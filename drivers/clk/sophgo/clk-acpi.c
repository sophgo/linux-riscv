// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 *
 * drivers/clk/clk-acpi.c - ACPI device clock resources support.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/clkdev.h>

#include "clk.h"

#ifdef CONFIG_ACPI
/**
 * struct acpi_clk_provider - Clock provider registration structure
 * @link: Entry in global list of clock providers
 * @fwnode: Pointer to firmware node handle of clock provider
 * @get: Get clock callback. Returns NULL or a struct clk for the given clock specifier
 * @get_hw: Get clk_hw callback. Returns NULL, ERR_PTR or a struct clk_hw for the given clock specifier
 * @data: context pointer to be passed into @get callback
 */
struct acpi_clk_provider {
    struct list_head link;
    struct fwnode_handle *fwnode;
    struct clk *(*get)(struct acpi_clk_lookup *clkspec, void *data);
    struct clk_hw *(*get_hw)(struct acpi_clk_lookup *clkspec, void *data);
    void *data;
};

static LIST_HEAD(acpi_clk_providers);
static DEFINE_MUTEX(acpi_clk_mutex);

struct acpi_clk_default_rate {
    struct list_head link;
    struct acpi_clk_lookup *clkspec;
	u32 rate;
};

static LIST_HEAD(acpi_clk_default_rates);
static DEFINE_MUTEX(acpi_clk_rate_mutex);

struct clk *acpi_clk_src_simple_get(struct acpi_clk_lookup *clkspec,
				     void *data)
{
	return data;
}
EXPORT_SYMBOL_GPL(acpi_clk_src_simple_get);

struct clk *acpi_clk_src_onecell_get(struct acpi_clk_lookup *clkspec, void *data)
{
	struct clk_onecell_data *clk_data = data;
	unsigned int idx = clkspec->clk_rs.rs_index;

	if (idx >= clk_data->clk_num) {
		pr_err("%s: invalid clock index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return clk_data->clks[idx];
}
EXPORT_SYMBOL_GPL(acpi_clk_src_onecell_get);

/**
 * fwnode_clk_add_provider() - Register a clock provider for a firmware node
 * @fwnode: Firmware node handle associated with clock provider
 * @clk_src_get: callback for decoding clock
 * @data: context pointer for @clk_src_get callback.
 */
int acpi_clk_add_provider(struct fwnode_handle *fwnode,
                            struct clk *(*clk_src_get)(struct acpi_clk_lookup *clkspec, void *data),
                            void *data)
{
    struct acpi_clk_provider *cp;
    int ret;

    if (!fwnode)
        return 0;

    cp = kzalloc(sizeof(*cp), GFP_KERNEL);
    if (!cp)
        return -ENOMEM;

    cp->fwnode = fwnode;
    cp->data = data;
    cp->get = clk_src_get;

    mutex_lock(&acpi_clk_mutex);
    list_add(&cp->link, &acpi_clk_providers);
    mutex_unlock(&acpi_clk_mutex);
    pr_debug("Added clock from %pfwN\n", fwnode);

    fwnode_dev_initialized(fwnode, true);

    return ret;
}
EXPORT_SYMBOL_GPL(acpi_clk_add_provider);

/**
 * fwnode_clk_del_provider() - Unregister a clock provider for a firmware node
 * @fwnode: Firmware node handle associated with clock provider
 */
void acpi_clk_del_provider(struct fwnode_handle *fwnode)
{
    struct acpi_clk_provider *cp, *tmp;

    mutex_lock(&acpi_clk_mutex);
    list_for_each_entry_safe(cp, tmp, &acpi_clk_providers, link) {
        if (cp->fwnode == fwnode) {
            list_del(&cp->link);
            fwnode_handle_put(cp->fwnode);
            kfree(cp);
            break;
        }
    }
    mutex_unlock(&acpi_clk_mutex);
}
EXPORT_SYMBOL_GPL(acpi_clk_del_provider);

static struct clk_hw *
__acpi_clk_get_hw_from_provider(struct acpi_clk_provider *provider,
			      struct acpi_clk_lookup *clkspec)
{
	struct clk *clk;

	if (provider->get_hw)
		return provider->get_hw(clkspec, provider->data);

	clk = provider->get(clkspec, provider->data);
	if (IS_ERR(clk))
		return ERR_CAST(clk);
	return __clk_get_hw(clk);
}

static struct clk_hw *
acpi_clk_get_hw_from_clkspec(struct acpi_clk_lookup *clkspec)
{
	struct acpi_clk_provider *provider;
	struct clk_hw *hw = ERR_PTR(-EPROBE_DEFER);

	if (!clkspec)
		return ERR_PTR(-EINVAL);

	mutex_lock(&acpi_clk_mutex);
	list_for_each_entry(provider, &acpi_clk_providers, link) {
		if (provider->fwnode == clkspec->clk_rs.fwnode) {
			hw = __acpi_clk_get_hw_from_provider(provider, clkspec);
			if (!IS_ERR(hw))
				break;
		}
	}
	mutex_unlock(&acpi_clk_mutex);

	return hw;
}

/**
 * acpi_clk_get_from_provider() - Lookup a clock from a clock provider
 * @clkspec: pointer to a clock specifier data structure
 *
 * This function looks up a struct clk from the registered list of clock
 * providers, an input is a clock specifier data structure as returned
 * from the fwnode_parse_phandle_with_args() function call.
 */
struct clk *acpi_clk_get_from_provider(struct acpi_clk_lookup *clkspec)
{
	struct clk_hw *hw = acpi_clk_get_hw_from_clkspec(clkspec);

	return clk_hw_create_clk(NULL, hw, NULL, __func__);
}
EXPORT_SYMBOL_GPL(acpi_clk_get_from_provider);

static struct fwnode_handle *acpi_get_clk_fwnode(char *path)
{
	acpi_handle handle;
	acpi_status status;
	struct acpi_device *device;

	status = acpi_get_handle(NULL, path, &handle);
	if (ACPI_FAILURE(status))
		return ERR_PTR(-ENODEV);

	device = acpi_get_acpi_dev(handle);
	if (WARN_ON(!device))
		return NULL;

	return &device->fwnode;
}

static uint64_t calc_clock_rate(u32 freq_num, u16 freq_denom, u8 scale)
{
	uint32_t scale_factor[3] = { 1, 1000, 1000000 };
	uint64_t rate = 0;

	if (scale < ARRAY_SIZE(scale_factor))
		rate = DIV_ROUND_UP(freq_num * scale_factor[scale], freq_denom);

	return rate;
}

int acpi_populate_clk_set_rate(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_clock_input *res = &ares->data.clock_input;
    struct acpi_clk_default_rate *cdr;
	struct acpi_clk_lookup *clkspec;
	u32 rate;

	if (ares->type != ACPI_RESOURCE_TYPE_CLOCK_INPUT)
		return 1;

	clkspec = kzalloc(sizeof(*clkspec), GFP_KERNEL);
    if (!clkspec)
        return -ENOMEM;

    memset(clkspec, 0, sizeof(struct acpi_clk_lookup));

	clkspec->mode = res->mode;
	clkspec->freq_div = res->frequency_divisor;
	clkspec->freq_num = res->frequency_numerator;
	clkspec->scale = res->scale;
	clkspec->clk_rs.fwnode = acpi_get_clk_fwnode(res->resource_source.string_ptr);
	clkspec->clk_rs.rs_index = res->resource_source.index;
	clkspec->found = true;
	clkspec->index++;
	clkspec->n++;

	rate = calc_clock_rate(clkspec->freq_num, clkspec->freq_div, clkspec->scale);

	if (rate) {
        cdr = kzalloc(sizeof(*cdr), GFP_KERNEL);
        if (!cdr)
            return -ENOMEM;

		cdr->clkspec = clkspec;
		cdr->rate = rate;

        mutex_lock(&acpi_clk_rate_mutex);
        list_add(&cdr->link, &acpi_clk_default_rates);
        mutex_unlock(&acpi_clk_rate_mutex);
	}

	return 1;
}

int acpi_set_default_clk_rates(void)
{
	struct acpi_clk_default_rate *clk_rate;
	struct clk *clk;
	int rc;

	mutex_lock(&acpi_clk_rate_mutex);
	list_for_each_entry(clk_rate, &acpi_clk_default_rates, link) {
		clk = acpi_clk_get_from_provider(clk_rate->clkspec);
		if (IS_ERR(clk)) {
		    pr_warn("clk: couldn't get clock index %d\n",
				    clk_rate->clkspec->index);
				return -EINVAL;
		}

		rc = clk_set_rate(clk, clk_rate->rate);
		if (rc < 0)
			pr_err("clk: couldn't set %s clk rate to %d (%d), current rate: %ld\n",
				    __clk_get_name(clk), clk_rate->rate, rc, clk_get_rate(clk));

		clk_put(clk);
		kfree(clk_rate->clkspec);
	}
	mutex_unlock(&acpi_clk_rate_mutex);

	return 0;
}

static int acpi_clk_property_lookup(struct fwnode_handle *fwnode,
				     const char *propname,
				     struct acpi_clk_lookup *lookup)
{
	const char **names;
	int i, ret, count;

	count = fwnode_property_string_array_count(fwnode, "clock-names");
	if (count < 0)
		return -EINVAL;

	if (count == 0) {
		pr_warn("%s no clock names\n", fwnode_get_name(fwnode));
		return -EINVAL;
	}

	names = kcalloc(count, sizeof(*names), GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	ret = fwnode_property_read_string_array(fwnode, "clock-names",
						names, count);
	if (ret < 0) {
		pr_warn("%s failed to read clock names\n", fwnode_get_name(fwnode));
		kfree(names);
		return ret;
	}

	for (i = 0; i < count; i++) {
		/*
		 * Allow overriding "fixed" names provided by the clock
		 * provider. The "fixed" names are more often than not
		 * generic and less informative than the names given in
		 * device properties.
		 */
		if (names[i] && names[i][0] && !strcmp(names[i], propname)) {
			lookup->index = i;
			break;
		}
	}

	if (i >= count) {
		lookup->index = -1;
		pr_warn("%s failed to get clock %s\n", fwnode_get_name(fwnode), propname);
		kfree(names);
		return -EINVAL;
	}

	kfree(names);

	return 0;
}

static int acpi_populate_clk_lookup(struct acpi_resource *ares, void *data)
{
	struct acpi_clk_lookup *lookup = data;
	struct acpi_resource_clock_input *res = &ares->data.clock_input;

	if (ares->type != ACPI_RESOURCE_TYPE_CLOCK_INPUT)
		return 1;

	if (lookup->n++ != lookup->index)
		return 1;

	lookup->mode = res->mode;
	lookup->freq_div = res->frequency_divisor;
	lookup->freq_num = res->frequency_numerator;
	lookup->scale = res->scale;
	lookup->clk_rs.fwnode = acpi_get_clk_fwnode(res->resource_source.string_ptr);
	lookup->clk_rs.rs_index = res->resource_source.index;
	lookup->found = true;

	return 1;
}

static int acpi_clk_resource_lookup(struct acpi_device *adev,
                     struct acpi_clk_lookup *lookup)
{
	struct list_head res_list;
	int ret;

	INIT_LIST_HEAD(&res_list);

	ret = acpi_dev_get_resources(adev, &res_list,
	                    acpi_populate_clk_lookup, lookup);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&res_list);

	if (!lookup->found)
		return -ENOENT;

	return 0;
}

/**
 * acpi_clk_get_hw() - get a clk hw from device resources
 * @fwnode: pointer to a ACPI device to get clk from
 * @index: index of ClockInput resource (starting from %0)
 * @con_id: Property name of the GPIO (optional)
 *
 * Function goes through ACPI resources for @fwnode and based on @index looks
 * up a ClockInput resource, translates it to the linux clk hw,
 * and returns it.
 *
 * If @con_id is specified the clk is looked using device property. In
 * that case @index is used to select the GPIO entry in the property value
 * (in case of multiple).
 *
 * If the clk cannot be translated or there is an error, an ERR_PTR is
 * returned.
 */
struct clk_hw *acpi_clk_get_hw(struct fwnode_handle *fwnode, int index,
			     const char *con_id)
{
	struct clk_hw *hw;
	struct acpi_clk_lookup clkspec;
	int ret;

	memset(&clkspec, 0, sizeof(clkspec));
	clkspec.index = index;

	// fix index by con_id
	if (con_id) {
		ret = acpi_clk_property_lookup(fwnode,
						con_id, &clkspec);
		if (ret)
			return ERR_PTR(ret);

		pr_debug("CLOCK %s: looking up %s, _DSD returned index %d\n", 
		                fwnode_get_name(fwnode), con_id, clkspec.index);
	} else {
		pr_debug("CLOCK %s: con_id is NULL.\n", 
		                fwnode_get_name(fwnode));
		return ERR_PTR(-EINVAL);
	}

	ret = acpi_clk_resource_lookup(to_acpi_device_node(fwnode), &clkspec);
	if (ret)
		return ERR_PTR(ret);

	hw = acpi_clk_get_hw_from_clkspec(&clkspec);
	fwnode_handle_put(clkspec.clk_rs.fwnode);

	return hw;
}
#endif
