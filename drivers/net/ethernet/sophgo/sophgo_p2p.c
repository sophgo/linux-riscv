// SPDX-License-Identifier: GPL-2.0
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/irqreturn.h>
#include <linux/mod_devicetable.h>
#include <linux/spinlock.h>
#include <linux/of.h>

#include "sophgo_common.h"
#include "sophgo_p2p.h"
#include "p2p_cdma_tcp.h"

static inline void intr_clear(struct veth_dev *vdev)
{
	u32 value;

	value = sg_read32(vdev->cdma_csr_reg, CDMA_CSR_20_SETTING);
	sg_write32(vdev->cdma_csr_reg, CDMA_CSR_20_SETTING, value);
}

static inline void p2p_disable_all_irq(void __iomem *csr_reg_base)
{
	sg_write32(csr_reg_base, CDMA_CSR_19_SETTING, 0xffffffff);
}

static inline void p2p_enable_all_irq(void __iomem *csr_reg_base)
{
	sg_write32(csr_reg_base, CDMA_CSR_19_SETTING, 0);
}

static irqreturn_t veth_irq(int irq, void *id)
{
	u32 status;
	struct veth_dev *vdev = id;

	status = sg_read32(vdev->cdma_csr_reg, CDMA_CSR_20_SETTING);
	intr_clear(vdev);

	if (((status >> INTR_TCP_RCV_CMD_DONE) & 0x1) == 1)
		napi_schedule(&vdev->napi);

	return IRQ_HANDLED;
}

static void cdma_soft_reset(struct veth_dev *vdev)
{
	u32 val = sg_read32(vdev->cxp_top_reg, 0x90);

	val &= ~(1 << CDMA0_WRAPPER_SOFT_RSTN);
	debug_log("[%s]val:%u", __func__, val);
	sg_write32(vdev->cxp_top_reg, 0x90, val);
	udelay(10);
	val |= (1 << CDMA0_WRAPPER_SOFT_RSTN);
	debug_log("[%s]val:%u", __func__, val);
	sg_write32(vdev->cxp_top_reg, 0x90, val);
}

static int bm1684x_veth_alloc_addr(struct veth_dev *vdev)
{
	int skb_len = ETH_MTU + 0xd;
	// struct eth_dev_info *veth = &bmdi->vir_eth;
	struct sk_buff *skb = NULL;
	void *skb_vaddr;
	u64 skb_paddr;
	size_t size;
	u32 quene_size = DESC_NUM;
	u32 cmd_id = CMD_ID_START;
	int i;
	struct net_device *ndev;

	ndev = vdev->ndev;

	vdev->rx_queue = kzalloc(sizeof(struct rx_queue), GFP_KERNEL);
	vdev->tx_queue = kzalloc(sizeof(struct tx_queue), GFP_KERNEL);

	vdev->desc_tx =
		(struct dma_tcp_send *)dma_alloc_coherent(&vdev->pdev->dev,
							  quene_size * sizeof(struct dma_tcp_send),
							  &vdev->desc_tx_phy,
							  GFP_KERNEL);
	if (!vdev->desc_tx)
		return -ENOMEM;

	vdev->desc_rx =
		(struct dma_tcp_rcv *)dma_alloc_coherent(&vdev->pdev->dev,
							 quene_size * sizeof(struct dma_tcp_rcv),
							 &vdev->desc_rx_phy,
							 GFP_KERNEL);
	if (!vdev->desc_rx)
		return -ENOMEM;

	debug_log("desc rx phy addr :0x%llx", vdev->desc_rx_phy);
	debug_log("desc tx phy addr :0x%llx", vdev->desc_tx_phy);
	debug_log("desc tx virtual addr :0x%llx", (u64)vdev->desc_tx);
	debug_log("desc tx virtual addr :0x%llx", (u64)vdev->desc_rx);

	size = quene_size * sizeof(struct buffer_info);
	vdev->rx_queue->buffer_info = (struct buffer_info *)vzalloc(size);
	if (!vdev->rx_queue->buffer_info) {
		pr_err("buffer_info vzalloc fail");
		return -ENOMEM;
	}

	struct dma_tcp_rcv *pdma_tcp_rcv = vdev->desc_rx;

	for (i = 0; i < quene_size; i++) {
		skb = netdev_alloc_skb(ndev, skb_len + NET_IP_ALIGN);
		skb_reserve(skb, NET_IP_ALIGN);  // align IP on 16B boundary
		skb_reset_mac_header(skb);
		skb_vaddr = skb_put(skb, skb_len);
		skb_paddr = virt_to_phys(skb_vaddr);

		p2p_tcp_rcv_cmd_config(pdma_tcp_rcv,
				       skb_paddr,
				       skb_len + NET_IP_ALIGN + CRC_LEN,
				       cmd_id++);

		vdev->rx_queue->buffer_info[i].skb = skb;
		vdev->rx_queue->buffer_info[i].paddr = skb_paddr;
		//pr_info("[CDMA_CMD_0]:0x%llx\n",vdev->desc_rx[i].desc0);

		pdma_tcp_rcv++;
	}
	vdev->rx_queue->cmd_id_cur = cmd_id;
	intr_clear(vdev);
	debug_log("cmd_id_cur:%u\n", vdev->rx_queue->cmd_id_cur);
	pr_info("rx/tx alloc mem complete!\n");
	return 0;
}

static int veth_open(struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int err;

	cdma_soft_reset(vdev);
	err = bm1684x_veth_alloc_addr(vdev);
	if (err == -ENOMEM) {
		pr_err("Coherent DMA alloc fail");
		return 0;
	}
	p2p_send_hw_init(vdev->cdma_csr_reg, vdev->desc_tx_phy, DESC_NUM);
	p2p_rcv_hw_init(vdev->cdma_csr_reg, vdev->desc_rx_phy, DESC_NUM);
	debug_log("cdma hw init complete!\n");
	//spin_lock_init(&vdev->lock_cdma);
	napi_enable(&vdev->napi);
	netif_carrier_on(ndev);
	netdev_reset_queue(ndev);

	p2p_enable_desc(vdev->cdma_csr_reg, TCP_RECEIVE);
	enable_irq(vdev->rx_irq);
	pr_info("p2p version = 1\n");
	netif_start_queue(ndev);
	return 0;
}

static int veth_close(struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int quene_size = DESC_NUM;

	debug_log("p2p close\n");

	napi_disable(&vdev->napi);
	debug_log("napi_disable\n");

	disable_irq(vdev->rx_irq);
	debug_log("disable_irq\n");

	netif_stop_queue(ndev);
	debug_log("netif_stop_queue\n");

	netif_carrier_off(ndev);
	debug_log("netif_carrier_off\n");

	//cmda soft reset
	cdma_soft_reset(vdev);
	debug_log("cdma_soft_reset\n");
	//release resource
	kfree(vdev->rx_queue);
	debug_log("kfree vdev->rx_queue\n");
	kfree(vdev->tx_queue);
	debug_log("kfree vdev->tx_queue\n");
	dma_free_coherent(&vdev->pdev->dev,
			  quene_size * sizeof(struct dma_tcp_send),
			  vdev->desc_tx,
			  vdev->desc_tx_phy);
	debug_log("dma_free_coherent tx\n");
	dma_free_coherent(&vdev->pdev->dev,
			  quene_size * sizeof(struct dma_tcp_rcv),
			  vdev->desc_rx,
			  vdev->desc_rx_phy);
	debug_log("dma_free_coherent rx\n");
	vfree(vdev->rx_queue->buffer_info);
	debug_log("vdev->rx_queue->buffer_info\n");

	return 0;
}

static int alloc_new_desc(struct veth_dev *vdev,
			  struct buffer_info *buffer_info,
			  struct dma_tcp_rcv *cur_rxd)
{
	struct sk_buff *skb_new;
	void *skb_vaddr;
	u64 skb_paddr;
	struct rx_queue *rx_q = vdev->rx_queue;
	struct net_device *ndev = vdev->ndev;

	skb_new = netdev_alloc_skb(ndev, SKB_LEN + NET_IP_ALIGN);
	if (!skb_new)
		return 1;
	skb_reserve(skb_new, NET_IP_ALIGN);  // align IP on 16B boundary
	skb_reset_mac_header(skb_new);
	skb_vaddr = skb_put(skb_new, SKB_LEN);
	skb_paddr = virt_to_phys(skb_vaddr);

	buffer_info->paddr = skb_paddr;
	buffer_info->skb = skb_new;

	p2p_tcp_rcv_cmd_config(cur_rxd,
			       skb_paddr,
			       SKB_LEN + NET_IP_ALIGN + CRC_LEN,
			       rx_q->cmd_id_cur++);//cmd_id max

	return 0;
}

static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	u32 entry;
	u32 need_to_clean;
	struct dma_tcp_send *dma_tcp_send;
	dma_addr_t dma_tcp_send_phy;
	u64 paddr;
	u32 cmd_id = 1;
	u64 skb_size;

	// netif_stop_queue(ndev);

	entry = vdev->tx_queue->tx_next_to_use;
	need_to_clean = vdev->tx_queue->tx_need_to_clean;

	paddr = virt_to_phys(skb->data);
	dma_tcp_send = vdev->desc_tx + entry;
	dma_tcp_send_phy = vdev->desc_tx_phy + entry * sizeof(struct dma_tcp_send);

	if (eth_skb_pad(skb)) {
		++ndev->stats.tx_dropped;
		pr_err("pad fail!\n");
		return NETDEV_TX_OK;
	}

	skb_size = skb->len;
	dma_sync_single_range_for_device(&vdev->pdev->dev, paddr, 0, skb_size, DMA_TO_DEVICE);
	debug_log("skb_size:%llu\n", skb_size);
	cmd_id = testcase_p2p_tcp_send_mode(vdev->cdma_csr_reg,
					    dma_tcp_send,
					    dma_tcp_send_phy,
					    paddr,
					    skb_size,
					    skb_size,
					    cmd_id);
	p2p_enable_desc(vdev->cdma_csr_reg, TCP_SEND);

	vdev->tx_queue->tx_next_to_use = (entry + 1) % DESC_NUM;
	cmd_id = (cmd_id + 1) % CDM_ID_MAX;
	++ndev->stats.tx_packets;
	ndev->stats.tx_bytes += skb->len;
	dev_consume_skb_any(skb);

	return NETDEV_TX_OK;
}

static int veth_rx(struct veth_dev *vdev, int limit)
{
	struct sk_buff *skb;
	struct dma_tcp_rcv *cur_rxd;
	struct dma_tcp_rcv *next_rxd;
	struct napi_struct *napi;
	struct net_device *ndev;
	struct rx_queue *rx_q = vdev->rx_queue;
	u64 timeout = 1500;
	u64 clean_count = 0;
	u64 count = 0;
	u32 length;
	u64 entry = rx_q->rx_next_to_use;
	u64 clean = rx_q->rx_next_to_clean;
	u32 cmd_id_rcv = sg_read32(vdev->cdma_csr_reg, TCP_CSR_13_SETTING);

	ndev = vdev->ndev;
	napi = &vdev->napi;

	debug_log("entry:%llu\n", entry);
	debug_log("cmd_id_rcv:%u\n", cmd_id_rcv);
	while (unlikely(sg_read32(vdev->cdma_csr_reg, TCP_CSR_13_SETTING) == entry)) {
		udelay(1);
		timeout--;
		if (timeout == 0) {
			napi_complete_done(napi, count);
			pr_err("cmd id timeout!\n");
			return count;
		}
	}

	cur_rxd = &vdev->desc_rx[entry];

	while (count < limit) {
		if (cur_rxd->own != 0 ||  cmd_id_rcv == entry) {
			napi_complete_done(napi, count);
			debug_log("napi_complete_done! count:%llu\n", count);
			break;
		}

		length = le16_to_cpu(cur_rxd->packet_length);
		debug_log("packet_length:%d\n", length);

		skb = rx_q->buffer_info[entry].skb;
		prefetch(skb);
		dma_sync_single_range_for_cpu(&vdev->pdev->dev,
					      rx_q->buffer_info[entry].paddr,
					      0,
					      length,
					      DMA_FROM_DEVICE);
		count++;
		clean_count++;

		if (unlikely(++entry >= DESC_NUM)) {
			while (clean != DESC_NUM) {
				if (alloc_new_desc(vdev,
						   &rx_q->buffer_info[clean],
						   &vdev->desc_rx[clean])) {
					pr_err("WARNING:no mem alloc_skb\n");
					napi_complete_done(napi, count);
					return count;
				}
				++clean;
			}
			entry = 0;
			clean_count = 0;
			clean = 0;
			p2p_enable_desc(vdev->cdma_csr_reg, TCP_RECEIVE);
			debug_log("refill desc\n");
		}

		next_rxd = &vdev->desc_rx[entry];
		prefetch(next_rxd);

		skb->protocol  = eth_type_trans(skb, napi->dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;// Determined by hardware
		length -= ETH_FCS_LEN;
		skb_trim(skb, length);
		napi_gro_receive(napi, skb);

		napi->dev->stats.rx_packets++;
		napi->dev->stats.rx_bytes += length;

		cur_rxd = next_rxd;
		cmd_id_rcv = sg_read32(vdev->cdma_csr_reg, TCP_CSR_13_SETTING);
	}

	cur_rxd = &vdev->desc_rx[clean];

	for (int i = 0; i < clean_count; i++) {
		if (alloc_new_desc(vdev, &rx_q->buffer_info[clean], cur_rxd)) {
			pr_err("no mem alloc_skb\n");
			break;
		}
		if (++clean == DESC_NUM)
			clean = 0;
		cur_rxd++;
	}

	rx_q->rx_next_to_use = entry;
	rx_q->rx_next_to_clean = clean;
	// pr_info("rx_q->rx_next_to_use:%d, rx_q->rx_next_to_clean:%d\n",
	//	    rx_q->rx_next_to_use, rx_q->rx_next_to_clean);

	return count;
}

static int p2p_set_eth_mac_addr(struct net_device *ndev, void *addr)
{
	int ret = 0;

	ret = eth_mac_addr(ndev, addr);

	//stmmac_set_umac_addr(priv, priv->hw, ndev->dev_addr, 0);

	return ret;
}

static void eth_ndo_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	//struct eth_dev_info *info = *((struct eth_dev_info **)netdev_priv(ndev));
	//dev_info(&info->pci_dev->dev, "Tx timeout\n");
	pr_err("Tx timeout\n");
	ndev->stats.tx_errors++;
	netif_wake_queue(ndev);
}

static const struct net_device_ops veth_ops = {
	.ndo_open = veth_open,
	.ndo_stop = veth_close,
	.ndo_start_xmit = veth_xmit,
	.ndo_set_mac_address = p2p_set_eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_tx_timeout = eth_ndo_tx_timeout,
	//.ndo_change_mtu = eth_change_mtu,
};

static int veth_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct veth_dev *vdev;
	int err;

	vdev = container_of(napi, struct veth_dev, napi);

	err = veth_rx(vdev, budget);

	return err;
}

static int sg_veth_get_resource(struct platform_device *pdev, struct veth_dev *vdev)
{
	int err;

	vdev->cdma_cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cdma_cfg");
	if (!vdev->cdma_cfg_res) {
		pr_err("cannot get resource - cdma reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->cdma_reg = ioremap(vdev->cdma_cfg_res->start,
				 vdev->cdma_cfg_res->end - vdev->cdma_cfg_res->start);
	if (!vdev->cdma_reg) {
		pr_err("map cdma cfg reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->cdma_csr_reg = vdev->cdma_reg + 0x1000;

	vdev->cxp_top_cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cxp_top_cfg");
	if (!vdev->cxp_top_cfg_res) {
		pr_err("cannot get resource - cxp top reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->cxp_top_reg = ioremap(vdev->cxp_top_cfg_res->start,
				    vdev->cxp_top_cfg_res->end - vdev->cxp_top_cfg_res->start);
	if (!vdev->cxp_top_reg) {
		pr_err("map cdma cfg reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->rx_irq = platform_get_irq_byname(pdev, "CXP_CDMA0");
	if (vdev->rx_irq < 0) {
		pr_err("no rx interrupt resource!\n");
		err = -ENOMEM;
		return err;
	}

	return 0;
}

static int sg_p2p_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct veth_dev *vdev;
	struct net_device *ndev;
	int err;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "sophgon, p2p");
	if (!of_device_is_available(node)) {
		pr_err("p2p driver node is disable!\n");
		err = -ENODEV;
		return err;
	}
	debug_log("of_find_compatible_node\n");
	dev = &pdev->dev;
	dma_set_mask(dev, DMA_BIT_MASK(48));
	ndev = alloc_etherdev(sizeof(struct veth_dev));
	if (!ndev) {
		pr_err("cannot allocate device instance !\n");
		return -ENOMEM;
	}
	debug_log("alloc_etherdev\n");
	ether_setup(ndev);
	debug_log("ether_setup\n");
	strncpy(ndev->name, "p2p%d", IFNAMSIZ);

	eth_hw_addr_random(ndev);
	debug_log("eth_hw_addr_random\n");

	vdev = netdev_priv(ndev);
	vdev->ndev = ndev;
	debug_log("netdev_priv\n");
	err = sg_veth_get_resource(pdev, vdev);
	if (err != 0)
		goto err_free_netdev;
	debug_log("sg_veth_get_resource\n");
	vdev->pdev = pdev;
	platform_set_drvdata(pdev, ndev);
	debug_log("platform_set_drvdata\n");
	ndev->netdev_ops = &veth_ops;
	ndev->irq = vdev->rx_irq;
	ndev->mtu = VETH_DEFAULT_MTU;

	netif_napi_add(ndev, &vdev->napi, veth_napi_poll_rx);
	debug_log("netif_napi_add\n");

	err = register_netdev(ndev);
	debug_log("register_netdev\n");
	if (err) {
		pr_err("register net device failed!\n");
		goto err_free_netdev;
	}

	intr_clear(vdev);
	if (devm_request_irq(&vdev->pdev->dev,
			     vdev->rx_irq,
			     veth_irq,
			     IRQF_SHARED,
			     "CXP_CDMA0",
			     vdev)) {
		pr_err("request rx irq failed!\n");
		return -1;
	}
	disable_irq(vdev->rx_irq);

	pr_info("p2p probe complete!\n");
	return 0;

err_free_netdev:
	free_netdev(ndev);
	return err;
}

static void sg_p2p_remove(struct platform_device *pdev)
{
	struct veth_dev *vdev;
	struct net_device *ndev;

	ndev = platform_get_drvdata(pdev);
	vdev = netdev_priv(ndev);
	debug_log("into remove\n");
	//netdev_info(pdev, "%s: removing driver", __func__);
	netif_carrier_off(ndev);
	debug_log("netif_carrier_off\n");
	unregister_netdev(ndev);
	debug_log("unregister_netdev\n");
	disable_irq(vdev->rx_irq);
	debug_log("disable_irq\n");
	free_netdev(ndev);
	pr_info("free_netdev\n");
	return;
}

static const struct of_device_id sg_p2p_match[] = {
	{.compatible = "sophgon, p2p"},
	{},
};

static struct platform_driver sg_p2p_driver = {
	.probe = sg_p2p_probe,
	.remove = sg_p2p_remove,
	.driver = {
		.name = "p2p",
		.of_match_table = sg_p2p_match,
	},
};

module_platform_driver(sg_p2p_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ChengJun Li <chengjun.li@sophon.com>");
MODULE_DESCRIPTION("Sophgon P2P Ethernet Driver over 100G eth");
