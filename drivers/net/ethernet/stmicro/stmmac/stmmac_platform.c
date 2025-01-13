// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  This contains the functions to handle the platform driver.

  Copyright (C) 2007-2011  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/fwnode_mdio.h>
#include <linux/property.h>
#include "stmmac.h"
#include "stmmac_platform.h"

#if defined(CONFIG_ACPI) || defined(CONFIG_OF)

/**
 * dwmac1000_validate_mcast_bins - validates the number of Multicast filter bins
 * @dev: struct device of the platform device
 * @mcast_bins: Multicast filtering bins
 * Description:
 * this function validates the number of Multicast filtering bins specified
 * by the configuration through the device tree. The Synopsys GMAC supports
 * 64 bins, 128 bins, or 256 bins. "bins" refer to the division of CRC
 * number space. 64 bins correspond to 6 bits of the CRC, 128 corresponds
 * to 7 bits, and 256 refers to 8 bits of the CRC. Any other setting is
 * invalid and will cause the filtering algorithm to use Multicast
 * promiscuous mode.
 */
static int dwmac1000_validate_mcast_bins(struct device *dev, int mcast_bins)
{
	int x = mcast_bins;

	switch (x) {
	case HASH_TABLE_SIZE:
	case 128:
	case 256:
		break;
	default:
		x = 0;
		dev_info(dev, "Hash table entries set to unexpected value %d\n",
			 mcast_bins);
		break;
	}
	return x;
}

/**
 * dwmac1000_validate_ucast_entries - validate the Unicast address entries
 * @dev: struct device of the platform device
 * @ucast_entries: number of Unicast address entries
 * Description:
 * This function validates the number of Unicast address entries supported
 * by a particular Synopsys 10/100/1000 controller. The Synopsys controller
 * supports 1..32, 64, or 128 Unicast filter entries for it's Unicast filter
 * logic. This function validates a valid, supported configuration is
 * selected, and defaults to 1 Unicast address if an unsupported
 * configuration is selected.
 */
static int dwmac1000_validate_ucast_entries(struct device *dev,
					    int ucast_entries)
{
	int x = ucast_entries;

	switch (x) {
	case 1 ... 32:
	case 64:
	case 128:
		break;
	default:
		x = 1;
		dev_info(dev, "Unicast table entries set to unexpected value %d\n",
			 ucast_entries);
		break;
	}
	return x;
}

/**
 * stmmac_axi_setup - parse DT ACPI object parameters for programming the AXI register
 * @pdev: platform device
 * Description:
 * if required, from device-tree the AXI internal register can be tuned
 * by using platform parameters.
 */
static struct stmmac_axi *stmmac_axi_setup(struct platform_device *pdev)
{
	// struct device_node *np;
	struct fwnode_handle *fwnode;
	struct stmmac_axi *axi;

	fwnode = fwnode_find_reference(dev_fwnode(&pdev->dev), "snps,axi-config", 0);
	if (IS_ERR(fwnode)) {
		dev_err(&pdev->dev, "No matching axi-config!\n");
		return NULL;
	}

	axi = devm_kzalloc(&pdev->dev, sizeof(*axi), GFP_KERNEL);
	if (!axi) {
		fwnode_handle_put(fwnode);
		return ERR_PTR(-ENOMEM);
	}

	axi->axi_lpi_en = fwnode_property_read_bool(fwnode, "snps,lpi_en");
	axi->axi_xit_frm = fwnode_property_read_bool(fwnode, "snps,xit_frm");
	axi->axi_kbbe = fwnode_property_read_bool(fwnode, "snps,kbbe");
	axi->axi_fb = fwnode_property_read_bool(fwnode, "snps,fb");
	axi->axi_mb = fwnode_property_read_bool(fwnode, "snps,mb");
	axi->axi_rb = fwnode_property_read_bool(fwnode, "snps,rb");

	if (fwnode_property_read_u32(fwnode, "snps,wr_osr_lmt", &axi->axi_wr_osr_lmt))
		axi->axi_wr_osr_lmt = 1;
	if (fwnode_property_read_u32(fwnode, "snps,rd_osr_lmt", &axi->axi_rd_osr_lmt))
		axi->axi_rd_osr_lmt = 1;
	fwnode_property_read_u32_array(fwnode, "snps,blen", axi->axi_blen, AXI_BLEN);
	fwnode_handle_put(fwnode);

	return axi;
}

/**
 * stmmac_mtl_setup - parse DT or ACPI object parameters for multiple queues configuration
 * @pdev: platform device
 * @plat: enet data
 */
static int stmmac_mtl_setup(struct platform_device *pdev,
			    struct plat_stmmacenet_data *plat)
{
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	struct fwnode_handle *q_node;
	struct fwnode_handle *rx_node;
	struct fwnode_handle *tx_node;
	u8 queue = 0;
	int ret = 0;

	/* For backwards-compatibility with device trees that don't have any
	 * snps,mtl-rx-config or snps,mtl-tx-config properties, we fall back
	 * to one RX and TX queues each.
	 */
	plat->rx_queues_to_use = 1;
	plat->tx_queues_to_use = 1;

	/* First Queue must always be in DCB mode. As MTL_QUEUE_DCB = 1 we need
	 * to always set this, otherwise Queue will be classified as AVB
	 * (because MTL_QUEUE_AVB = 0).
	 */
	plat->rx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;

	rx_node = fwnode_find_reference(fwnode, "snps,mtl-rx-config", 0);
	if (IS_ERR(rx_node)) {
		dev_err(&pdev->dev, "Not find mtl-rx-config!\n");
		return ret;
	}

	tx_node = fwnode_find_reference(fwnode, "snps,mtl-tx-config", 0);
	if (IS_ERR(tx_node)) {
		dev_err(&pdev->dev, "Not find mtl-rx-config!\n");
		fwnode_handle_put(rx_node);
		return ret;
	}

	/* Processing RX queues common config */
	if (fwnode_property_read_u32(rx_node, "snps,rx-queues-to-use",
				 &plat->rx_queues_to_use))
		plat->rx_queues_to_use = 1;

	if (fwnode_property_read_bool(rx_node, "snps,rx-sched-sp"))
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	else if (fwnode_property_read_bool(rx_node, "snps,rx-sched-wsp"))
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_WSP;
	else
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;

	/* Processing individual RX queue config */
	fwnode_for_each_child_node(rx_node, q_node) {
		if (queue >= plat->rx_queues_to_use)
			break;

		if (fwnode_property_read_bool(q_node, "snps,dcb-algorithm"))
			plat->rx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;
		else if (fwnode_property_read_bool(q_node, "snps,avb-algorithm"))
			plat->rx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;
		else
			plat->rx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;

		if (fwnode_property_read_u32(q_node, "snps,map-to-dma-channel",
					 &plat->rx_queues_cfg[queue].chan))
			plat->rx_queues_cfg[queue].chan = queue;
		/* TODO: Dynamic mapping to be included in the future */

		if (fwnode_property_read_u32(q_node, "snps,priority",
					&plat->rx_queues_cfg[queue].prio)) {
			plat->rx_queues_cfg[queue].prio = 0;
			plat->rx_queues_cfg[queue].use_prio = false;
		} else {
			plat->rx_queues_cfg[queue].use_prio = true;
		}

		/* RX queue specific packet type routing */
		if (fwnode_property_read_bool(q_node, "snps,route-avcp"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_AVCPQ;
		else if (fwnode_property_read_bool(q_node, "snps,route-ptp"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_PTPQ;
		else if (fwnode_property_read_bool(q_node, "snps,route-dcbcp"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_DCBCPQ;
		else if (fwnode_property_read_bool(q_node, "snps,route-up"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_UPQ;
		else if (fwnode_property_read_bool(q_node, "snps,route-multi-broad"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_MCBCQ;
		else
			plat->rx_queues_cfg[queue].pkt_route = 0x0;

		queue++;
	}
	if (queue != plat->rx_queues_to_use) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Not all RX queues were configured\n");
		goto out;
	}

	/* Processing TX queues common config */
	if (fwnode_property_read_u32(tx_node, "snps,tx-queues-to-use",
				 &plat->tx_queues_to_use))
		plat->tx_queues_to_use = 1;

	if (fwnode_property_read_bool(tx_node, "snps,tx-sched-wrr"))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	else if (fwnode_property_read_bool(tx_node, "snps,tx-sched-wfq"))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WFQ;
	else if (fwnode_property_read_bool(tx_node, "snps,tx-sched-dwrr"))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_DWRR;
	else
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_SP;

	queue = 0;

	/* Processing individual TX queue config */
	fwnode_for_each_child_node(tx_node, q_node) {
		if (queue >= plat->tx_queues_to_use)
			break;

		if (fwnode_property_read_u32(q_node, "snps,weight",
					 &plat->tx_queues_cfg[queue].weight))
			plat->tx_queues_cfg[queue].weight = 0x10 + queue;

		if (fwnode_property_read_bool(q_node, "snps,dcb-algorithm")) {
			plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;
		} else if (fwnode_property_read_bool(q_node,
						 "snps,avb-algorithm")) {
			plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;

			/* Credit Base Shaper parameters used by AVB */
			if (fwnode_property_read_u32(q_node, "snps,send_slope",
				&plat->tx_queues_cfg[queue].send_slope))
				plat->tx_queues_cfg[queue].send_slope = 0x0;
			if (fwnode_property_read_u32(q_node, "snps,idle_slope",
				&plat->tx_queues_cfg[queue].idle_slope))
				plat->tx_queues_cfg[queue].idle_slope = 0x0;
			if (fwnode_property_read_u32(q_node, "snps,high_credit",
				&plat->tx_queues_cfg[queue].high_credit))
				plat->tx_queues_cfg[queue].high_credit = 0x0;
			if (fwnode_property_read_u32(q_node, "snps,low_credit",
				&plat->tx_queues_cfg[queue].low_credit))
				plat->tx_queues_cfg[queue].low_credit = 0x0;
		} else {
			plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;
		}

		if (fwnode_property_read_u32(q_node, "snps,priority",
					&plat->tx_queues_cfg[queue].prio)) {
			plat->tx_queues_cfg[queue].prio = 0;
			plat->tx_queues_cfg[queue].use_prio = false;
		} else {
			plat->tx_queues_cfg[queue].use_prio = true;
		}

		plat->tx_queues_cfg[queue].coe_unsupported =
			fwnode_property_read_bool(q_node, "snps,coe-unsupported");

		queue++;
	}
	if (queue != plat->tx_queues_to_use) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Not all TX queues were configured\n");
		goto out;
	}

out:
	fwnode_handle_put(rx_node);
	fwnode_handle_put(tx_node);
	fwnode_handle_put(q_node);

	return ret;
}

/**
 * stmmac_get_mdio() - Gets the MDIO bus.
 *
 * The MDIO bus will be searched for in the following ways:
 * 1. The compatible is "snps,dwc-qos-ethernet-4.10" && a "mdio" named
 *    child node exists
 * 2. A child node with the "snps,dwmac-mdio" compatible is present
 *
 * Return: The MDIO node if present otherwise NULL
 */
static struct fwnode_handle *stmmac_get_mdio(struct plat_stmmacenet_data *plat,
			 struct device *dev)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_handle *mdio_node = NULL;

	if (fwnode_device_is_compatible(fwnode, "snps,dwc-qos-ethernet-4.10")) {
		mdio_node = fwnode_get_named_child_node(fwnode, "mdio");
	} else {
		/**
		 * If snps,dwmac-mdio is passed from DT, always register
		 * the MDIO
		 */
		fwnode_for_each_child_node(fwnode, mdio_node) {
			if (fwnode_device_is_compatible(mdio_node,
						    "snps,dwmac-mdio"))
				break;
		}
	}

	return mdio_node;
}

/**
 * stmmac_mdio_setup() - Populate platform related MDIO structures.
 * @plat: driver data platform structure
 * @np: devicetree node
 * @dev: device pointer
 *
 * This searches for MDIO information from the devicetree.
 * If an MDIO node is found, it's assigned to plat->mdio_node and
 * plat->mdio_bus_data is allocated.
 * If no connection can be determined, just plat->mdio_bus_data is allocated
 * to indicate a bus should be created and scanned for a phy.
 * If it's determined there's no MDIO bus needed, both are left NULL.
 *
 * This expects that plat->phy_node has already been searched for.
 *
 * Return: 0 on success, errno otherwise.
 */
static int stmmac_mdio_setup(struct plat_stmmacenet_data *plat, struct device *dev)
{
	bool legacy_mdio;
	struct fwnode_handle *fwnode = dev_fwnode(dev);

	plat->mdio_node = stmmac_get_mdio(plat, dev);
	if (plat->mdio_node)
		dev_info(dev, "Found MDIO subnode\n");

	/* Legacy devicetrees allowed for no MDIO bus description and expect
	 * the bus to be scanned for devices. If there's no phy or fixed-link
	 * described assume this is the case since there must be something
	 * connected to the MAC.
	 */
	legacy_mdio = !fwnode_phy_is_fixed_link(fwnode) && !plat->phy_node;
	if (legacy_mdio)
		dev_info(dev, "Deprecated MDIO bus assumption used\n");

	if (plat->mdio_node || legacy_mdio) {
		plat->mdio_bus_data = devm_kzalloc(dev,
						   sizeof(*plat->mdio_bus_data),
						   GFP_KERNEL);
		if (!plat->mdio_bus_data)
			return -ENOMEM;

		plat->mdio_bus_data->needs_reset = true;
	}

	return 0;
}

/**
 * stmmac_get_mac_mode - retrieves the interface of the MAC
 * @np: - device-tree node
 * Description:
 * Similar to `of_get_phy_mode()`, this function will retrieve (from
 * the device-tree) the interface mode on the MAC side. This assumes
 * that there is mode converter in-between the MAC & PHY
 * (e.g. GMII-to-RGMII).
 */
static int stmmac_get_mac_mode(struct fwnode_handle *fwnode)
{
	const char *pm;
	int err, i;

	err = fwnode_property_read_string(fwnode, "mac-mode", &pm);
	if (err < 0)
		return err;

	for (i = 0; i < PHY_INTERFACE_MODE_MAX; i++) {
		if (!strcasecmp(pm, phy_modes(i)))
			return i;
	}

	return -ENODEV;
}

/**
 * stmmac_probe_config_dt - parse device-tree driver parameters
 * @pdev: platform_device structure
 * @mac: MAC address to use
 * Description:
 * this function is to read the driver parameters from device-tree and
 * set some private fields that will be used by the main at runtime.
 */
struct plat_stmmacenet_data *
stmmac_probe_config_dt(struct platform_device *pdev, u8 *mac)
{
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	struct plat_stmmacenet_data *plat;
	struct stmmac_dma_cfg *dma_cfg;
	static int bus_id = -ENODEV;
	int phy_mode;
	void *ret;
	int rc;

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return ERR_PTR(-ENOMEM);

	rc = fwnode_get_mac_address(fwnode, mac);
	if (rc) {
		if (rc == -EPROBE_DEFER)
			return ERR_PTR(rc);

		eth_zero_addr(mac);
	}

	phy_mode = device_get_phy_mode(&pdev->dev);
	if (phy_mode < 0)
		return ERR_PTR(phy_mode);

	plat->phy_interface = phy_mode;
	rc = stmmac_get_mac_mode(fwnode);
	plat->mac_interface = rc < 0 ? plat->phy_interface : rc;

	/* Some wrapper drivers still rely on phy_node. Let's save it while
	 * they are not converted to phylink. */
	plat->phy_node = fwnode_find_reference(fwnode, "phy-handle", 0);
	if (IS_ERR(plat->phy_node)) {
		dev_err(&pdev->dev, "Not find phy-handle!\n");
		return ret;
	}

	/* PHYLINK automatically parses the phy-handle property */
	plat->port_node = fwnode;

	/* Get max speed of operation from device tree */
	fwnode_property_read_u32(fwnode, "max-speed", &plat->max_speed);

	if (fwnode_property_read_u32(fwnode, "bus_id", &plat->bus_id)!=0 &&
				     is_of_node(fwnode))
		plat->bus_id = of_alias_get_id(to_of_node(fwnode), "ethernet");
	if (plat->bus_id < 0) {
		if (bus_id < 0)
			bus_id = of_alias_get_highest_id("ethernet");
		/* No ethernet alias found, init at -1 so first bus_id is 0 */
		if (bus_id < 0)
			bus_id = -1;
		plat->bus_id = ++bus_id;
	}

	/* Default to phy auto-detection */
	plat->phy_addr = -1;

	/* Default to get clk_csr from stmmac_clk_csr_set(),
	 * or get clk_csr from device tree.
	 */
	plat->clk_csr = -1;
	if (fwnode_property_read_u32(fwnode, "snps,clk-csr", &plat->clk_csr))
		fwnode_property_read_u32(fwnode, "clk_csr", &plat->clk_csr);

	/* "snps,phy-addr" is not a standard property. Mark it as deprecated
	 * and warn of its use. Remove this when phy node support is added.
	 */
	if (fwnode_property_read_u32(fwnode, "snps,phy-addr", &plat->phy_addr) == 0)
		dev_warn(&pdev->dev, "snps,phy-addr property is deprecated\n");

	rc = stmmac_mdio_setup(plat, &pdev->dev);
	if (rc)
		return ERR_PTR(rc);

	fwnode_property_read_u32(fwnode, "tx-fifo-depth", &plat->tx_fifo_size);

	fwnode_property_read_u32(fwnode, "rx-fifo-depth", &plat->rx_fifo_size);

	plat->force_sf_dma_mode =
		fwnode_property_read_bool(fwnode, "snps,force_sf_dma_mode");

	if (fwnode_property_read_bool(fwnode, "snps,en-tx-lpi-clockgating"))
		plat->flags |= STMMAC_FLAG_EN_TX_LPI_CLOCKGATING;

	/* Set the maxmtu to a default of JUMBO_LEN in case the
	 * parameter is not present in the device tree.
	 */
	plat->maxmtu = JUMBO_LEN;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/*
	 * Currently only the properties needed on SPEAr600
	 * are provided. All other properties should be added
	 * once needed on other platforms.
	 */
	if (device_is_compatible(&pdev->dev, "st,spear600-gmac") ||
		device_is_compatible(&pdev->dev, "snps,dwmac-3.50a") ||
		device_is_compatible(&pdev->dev, "snps,dwmac-3.70a") ||
		device_is_compatible(&pdev->dev, "snps,dwmac")) {
		/* Note that the max-frame-size parameter as defined in the
		 * ePAPR v1.1 spec is defined as max-frame-size, it's
		 * actually used as the IEEE definition of MAC Client
		 * data, or MTU. The ePAPR specification is confusing as
		 * the definition is max-frame-size, but usage examples
		 * are clearly MTUs
		 */
		fwnode_property_read_u32(fwnode, "max-frame-size", &plat->maxmtu);
		fwnode_property_read_u32(fwnode, "snps,multicast-filter-bins",
				     &plat->multicast_filter_bins);
		fwnode_property_read_u32(fwnode, "snps,perfect-filter-entries",
				     &plat->unicast_filter_entries);
		plat->unicast_filter_entries = dwmac1000_validate_ucast_entries(
				&pdev->dev, plat->unicast_filter_entries);
		plat->multicast_filter_bins = dwmac1000_validate_mcast_bins(
				&pdev->dev, plat->multicast_filter_bins);
		plat->has_gmac = 1;
		plat->pmt = 1;
	}

	if (device_is_compatible(&pdev->dev, "snps,dwmac-3.40a")) {
		plat->has_gmac = 1;
		plat->enh_desc = 1;
		plat->tx_coe = 1;
		plat->bugged_jumbo = 1;
		plat->pmt = 1;
	}

	if (device_is_compatible(&pdev->dev, "snps,dwmac-4.00") ||
	    device_is_compatible(&pdev->dev, "snps,dwmac-4.10a") ||
	    device_is_compatible(&pdev->dev, "snps,dwmac-4.20a") ||
	    device_is_compatible(&pdev->dev, "snps,dwmac-5.10a") ||
	    device_is_compatible(&pdev->dev, "snps,dwmac-5.20")) {
		plat->has_gmac4 = 1;
		plat->has_gmac = 0;
		plat->pmt = 1;
		if (fwnode_property_read_bool(fwnode, "snps,tso"))
			plat->flags |= STMMAC_FLAG_TSO_EN;
	}

	if (device_is_compatible(&pdev->dev, "snps,dwmac-3.610") ||
		device_is_compatible(&pdev->dev, "snps,dwmac-3.710")) {
		plat->enh_desc = 1;
		plat->bugged_jumbo = 1;
		plat->force_sf_dma_mode = 1;
	}

	if (device_is_compatible(&pdev->dev, "snps,dwxgmac")) {
		plat->has_xgmac = 1;
		plat->pmt = 1;
		if (fwnode_property_read_bool(fwnode, "snps,tso"))
			plat->flags |= STMMAC_FLAG_TSO_EN;
	}

	dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*dma_cfg),
			       GFP_KERNEL);
	if (!dma_cfg) {
		stmmac_remove_config_dt(pdev, plat);
		return ERR_PTR(-ENOMEM);
	}
	plat->dma_cfg = dma_cfg;

	fwnode_property_read_u32(fwnode, "snps,pbl", &dma_cfg->pbl);
	if (!dma_cfg->pbl)
		dma_cfg->pbl = DEFAULT_DMA_PBL;
	fwnode_property_read_u32(fwnode, "snps,txpbl", &dma_cfg->txpbl);
	fwnode_property_read_u32(fwnode, "snps,rxpbl", &dma_cfg->rxpbl);
	dma_cfg->pblx8 = !fwnode_property_read_bool(fwnode, "snps,no-pbl-x8");

	dma_cfg->aal = fwnode_property_read_bool(fwnode, "snps,aal");
	dma_cfg->fixed_burst = fwnode_property_read_bool(fwnode, "snps,fixed-burst");
	dma_cfg->mixed_burst = fwnode_property_read_bool(fwnode, "snps,mixed-burst");

	plat->force_thresh_dma_mode = fwnode_property_read_bool(fwnode, "snps,force_thresh_dma_mode");
	if (plat->force_thresh_dma_mode && plat->force_sf_dma_mode) {
		plat->force_sf_dma_mode = 0;
		dev_warn(&pdev->dev,
			 "force_sf_dma_mode is ignored if force_thresh_dma_mode is set.\n");
	}

	fwnode_property_read_u32(fwnode, "snps,ps-speed", &plat->mac_port_sel_speed);

	plat->axi = stmmac_axi_setup(pdev);

	rc = stmmac_mtl_setup(pdev, plat);
	if (rc) {
		stmmac_remove_config_dt(pdev, plat);
		return ERR_PTR(rc);
	}

	/* clock setup */
	if (!device_is_compatible(&pdev->dev, "snps,dwc-qos-ethernet-4.10")) {
		if (is_of_node(fwnode)){
			plat->stmmac_clk = devm_clk_get(&pdev->dev,
							STMMAC_RESOURCE_NAME);
			if (IS_ERR(plat->stmmac_clk)) {
				dev_warn(&pdev->dev, "Cannot get CSR clock\n");
				plat->stmmac_clk = NULL;
			}
			clk_prepare_enable(plat->stmmac_clk);
		}
	}

	plat->pclk = devm_clk_get_optional(&pdev->dev, "pclk");
	if (IS_ERR(plat->pclk)) {
		ret = plat->pclk;
		goto error_pclk_get;
	}
	clk_prepare_enable(plat->pclk);

	/* Fall-back to main clock in case of no PTP ref is passed */
	if (is_of_node(fwnode)) {
		plat->clk_ptp_ref = devm_clk_get(&pdev->dev, "ptp_ref");
		if (IS_ERR(plat->clk_ptp_ref)) {
			plat->clk_ptp_rate = clk_get_rate(plat->stmmac_clk);
			dev_info(&pdev->dev, "PTP rate %d\n", plat->clk_ptp_rate);
			plat->clk_ptp_ref = NULL;
			dev_info(&pdev->dev, "PTP uses main clock\n");
		} else {
			plat->clk_ptp_rate = clk_get_rate(plat->clk_ptp_ref);
			dev_dbg(&pdev->dev, "PTP rate %d\n", plat->clk_ptp_rate);
		}
	} else {
		plat->clk_ptp_rate = 50000000;
		dev_info(&pdev->dev, "PTP rate %d\n", plat->clk_ptp_rate);
	}


	plat->stmmac_rst = devm_reset_control_get_optional(&pdev->dev,
							   STMMAC_RESOURCE_NAME);
	if (IS_ERR(plat->stmmac_rst)) {
		ret = plat->stmmac_rst;
		goto error_hw_init;
	}

	plat->stmmac_ahb_rst = devm_reset_control_get_optional_shared(
							&pdev->dev, "ahb");
	if (IS_ERR(plat->stmmac_ahb_rst)) {
		ret = plat->stmmac_ahb_rst;
		goto error_hw_init;
	}

	return plat;

error_hw_init:
	clk_disable_unprepare(plat->pclk);
error_pclk_get:
	clk_disable_unprepare(plat->stmmac_clk);

	return ret;
}

static void devm_stmmac_remove_config_dt(void *data)
{
	struct plat_stmmacenet_data *plat = data;

	/* Platform data argument is unused */
	stmmac_remove_config_dt(NULL, plat);
}

/**
 * devm_stmmac_probe_config_dt
 * @pdev: platform_device structure
 * @mac: MAC address to use
 * Description: Devres variant of stmmac_probe_config_dt(). Does not require
 * the user to call stmmac_remove_config_dt() at driver detach.
 */
struct plat_stmmacenet_data *
devm_stmmac_probe_config_dt(struct platform_device *pdev, u8 *mac)
{
	struct plat_stmmacenet_data *plat;
	int ret;

	plat = stmmac_probe_config_dt(pdev, mac);
	if (IS_ERR(plat))
		return plat;

	ret = devm_add_action_or_reset(&pdev->dev,
				       devm_stmmac_remove_config_dt, plat);
	if (ret)
		return ERR_PTR(ret);

	return plat;
}

/**
 * stmmac_remove_config_dt - undo the effects of stmmac_probe_config_dt()
 * @pdev: platform_device structure
 * @plat: driver data platform structure
 *
 * Release resources claimed by stmmac_probe_config_dt().
 */
void stmmac_remove_config_dt(struct platform_device *pdev,
			     struct plat_stmmacenet_data *plat)
{
	clk_disable_unprepare(plat->stmmac_clk);
	clk_disable_unprepare(plat->pclk);
	fwnode_handle_put(plat->phy_node);
	fwnode_handle_put(plat->mdio_node);
}
#else
struct plat_stmmacenet_data *
stmmac_probe_config_dt(struct platform_device *pdev, u8 *mac)
{
	return ERR_PTR(-EINVAL);
}

struct plat_stmmacenet_data *
devm_stmmac_probe_config_dt(struct platform_device *pdev, u8 *mac)
{
	return ERR_PTR(-EINVAL);
}

void stmmac_remove_config_dt(struct platform_device *pdev,
			     struct plat_stmmacenet_data *plat)
{
}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(stmmac_probe_config_dt);
EXPORT_SYMBOL_GPL(devm_stmmac_probe_config_dt);
EXPORT_SYMBOL_GPL(stmmac_remove_config_dt);

int stmmac_get_platform_resources(struct platform_device *pdev,
				  struct stmmac_resources *stmmac_res)
{
	memset(stmmac_res, 0, sizeof(*stmmac_res));

	/* Get IRQ information early to have an ability to ask for deferred
	 * probe if needed before we went too far with resource allocation.
	 */
	stmmac_res->irq = platform_get_irq_byname(pdev, "macirq");
	if (stmmac_res->irq < 0)
		return stmmac_res->irq;

	/* On some platforms e.g. SPEAr the wake up irq differs from the mac irq
	 * The external wake up irq can be passed through the platform code
	 * named as "eth_wake_irq"
	 *
	 * In case the wake up interrupt is not passed from the platform
	 * so the driver will continue to use the mac irq (ndev->irq)
	 */
	stmmac_res->wol_irq =
		platform_get_irq_byname_optional(pdev, "eth_wake_irq");
	if (stmmac_res->wol_irq < 0) {
		if (stmmac_res->wol_irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(&pdev->dev, "IRQ eth_wake_irq not found\n");
		stmmac_res->wol_irq = stmmac_res->irq;
	}

	stmmac_res->lpi_irq =
		platform_get_irq_byname_optional(pdev, "eth_lpi");
	if (stmmac_res->lpi_irq < 0) {
		if (stmmac_res->lpi_irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(&pdev->dev, "IRQ eth_lpi not found\n");
	}

	stmmac_res->sfty_irq =
		platform_get_irq_byname_optional(pdev, "sfty");
	if (stmmac_res->sfty_irq < 0) {
		if (stmmac_res->sfty_irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_info(&pdev->dev, "IRQ sfty not found\n");
	}

	stmmac_res->addr = devm_platform_ioremap_resource(pdev, 0);

	return PTR_ERR_OR_ZERO(stmmac_res->addr);
}
EXPORT_SYMBOL_GPL(stmmac_get_platform_resources);

/**
 * stmmac_pltfr_init
 * @pdev: pointer to the platform device
 * @plat: driver data platform structure
 * Description: Call the platform's init callback (if any) and propagate
 * the return value.
 */
static int stmmac_pltfr_init(struct platform_device *pdev,
			     struct plat_stmmacenet_data *plat)
{
	int ret = 0;

	if (plat->init)
		ret = plat->init(pdev, plat->bsp_priv);

	return ret;
}

/**
 * stmmac_pltfr_exit
 * @pdev: pointer to the platform device
 * @plat: driver data platform structure
 * Description: Call the platform's exit callback (if any).
 */
static void stmmac_pltfr_exit(struct platform_device *pdev,
			      struct plat_stmmacenet_data *plat)
{
	if (plat->exit)
		plat->exit(pdev, plat->bsp_priv);
}

/**
 * stmmac_pltfr_probe
 * @pdev: platform device pointer
 * @plat: driver data platform structure
 * @res: stmmac resources structure
 * Description: This calls the platform's init() callback and probes the
 * stmmac driver.
 */
int stmmac_pltfr_probe(struct platform_device *pdev,
		       struct plat_stmmacenet_data *plat,
		       struct stmmac_resources *res)
{
	int ret;

	ret = stmmac_pltfr_init(pdev, plat);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat, res);
	if (ret) {
		stmmac_pltfr_exit(pdev, plat);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(stmmac_pltfr_probe);

static void devm_stmmac_pltfr_remove(void *data)
{
	struct platform_device *pdev = data;

	stmmac_pltfr_remove(pdev);
}

/**
 * devm_stmmac_pltfr_probe
 * @pdev: pointer to the platform device
 * @plat: driver data platform structure
 * @res: stmmac resources
 * Description: Devres variant of stmmac_pltfr_probe(). Allows users to skip
 * calling stmmac_pltfr_remove() on driver detach.
 */
int devm_stmmac_pltfr_probe(struct platform_device *pdev,
			    struct plat_stmmacenet_data *plat,
			    struct stmmac_resources *res)
{
	int ret;

	ret = stmmac_pltfr_probe(pdev, plat, res);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&pdev->dev, devm_stmmac_pltfr_remove,
					pdev);
}
EXPORT_SYMBOL_GPL(devm_stmmac_pltfr_probe);

/**
 * stmmac_pltfr_remove
 * @pdev: pointer to the platform device
 * Description: This undoes the effects of stmmac_pltfr_probe() by removing the
 * driver and calling the platform's exit() callback.
 */
void stmmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct plat_stmmacenet_data *plat = priv->plat;

	stmmac_dvr_remove(&pdev->dev);
	stmmac_pltfr_exit(pdev, plat);
}
EXPORT_SYMBOL_GPL(stmmac_pltfr_remove);

/**
 * stmmac_pltfr_suspend
 * @dev: device pointer
 * Description: this function is invoked when suspend the driver and it direcly
 * call the main suspend function and then, if required, on some platform, it
 * can call an exit helper.
 */
static int __maybe_unused stmmac_pltfr_suspend(struct device *dev)
{
	int ret;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);

	ret = stmmac_suspend(dev);
	stmmac_pltfr_exit(pdev, priv->plat);

	return ret;
}

/**
 * stmmac_pltfr_resume
 * @dev: device pointer
 * Description: this function is invoked when resume the driver before calling
 * the main resume function, on some platforms, it can call own init helper
 * if required.
 */
static int __maybe_unused stmmac_pltfr_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	ret = stmmac_pltfr_init(pdev, priv->plat);
	if (ret)
		return ret;

	return stmmac_resume(dev);
}

static int __maybe_unused stmmac_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	stmmac_bus_clks_config(priv, false);

	return 0;
}

static int __maybe_unused stmmac_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	return stmmac_bus_clks_config(priv, true);
}

static int __maybe_unused stmmac_pltfr_noirq_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;

	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* Disable clock in case of PWM is off */
		clk_disable_unprepare(priv->plat->clk_ptp_ref);

		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
	}

	return 0;
}

static int __maybe_unused stmmac_pltfr_noirq_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;

	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* enable the clk previously disabled */
		ret = pm_runtime_force_resume(dev);
		if (ret)
			return ret;

		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0) {
			netdev_warn(priv->dev,
				    "failed to enable PTP reference clock: %pe\n",
				    ERR_PTR(ret));
			return ret;
		}
	}

	return 0;
}

const struct dev_pm_ops stmmac_pltfr_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stmmac_pltfr_suspend, stmmac_pltfr_resume)
	SET_RUNTIME_PM_OPS(stmmac_runtime_suspend, stmmac_runtime_resume, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(stmmac_pltfr_noirq_suspend, stmmac_pltfr_noirq_resume)
};
EXPORT_SYMBOL_GPL(stmmac_pltfr_pm_ops);

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet platform support");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
