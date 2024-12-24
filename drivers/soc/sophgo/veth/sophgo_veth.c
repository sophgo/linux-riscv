// SPDX-License-Identifier: GPL-2.0
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
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "sophgo_veth.h"

static struct proc_dir_entry *sgdrv_proc_dir;
static const char debug_node_name[] = "sgdrv";
static struct proc_dir_entry *sgdrv_vethip;

// #define VETH_IRQ
// #define USE_CDMA
static int set_ready_flag(struct veth_dev *vdev);
void *vaddr_tx;
void *vaddr_rx;

static void sg_write32(void __iomem *base, u32 offset, u32 value)
{
	iowrite32(value, (void __iomem *)(((unsigned long)base) + offset));
}

static u32 sg_read32(void __iomem *base, u32 offset)
{
	return ioread32((void __iomem *)(((unsigned long)base) + offset));
}

static inline void intr_clear(struct veth_dev *vdev)
{
	sg_write32(vdev->irq_mem, 0x24, 0);
}

static irqreturn_t veth_irq(int irq, void *id)
{
	struct veth_dev *vdev = id;

	pr_info("receive irq!!!!!!\n");
	if (atomic_read(&vdev->link)) {
		// if (pt_load_rx(vdev->pt)) {
		// napi_schedule(&vdev->napi);
		// }
		napi_schedule(&vdev->napi);
	}
	intr_clear(vdev);
	return IRQ_HANDLED;
}

static void __maybe_unused sg_enable_eth_irq(struct veth_dev *vdev)
{
	// u32 intc_enable;
	// u32 intc_mask;

	// intc_enable = sg_read32(vdev->intc_cfg_reg, 0x4);
	// intc_enable |= (1 << 18);
	// sg_write32(vdev->intc_cfg_reg, 0x4, intc_enable);
	// intc_mask = sg_read32(vdev->intc_cfg_reg, 0xc);
	// intc_mask &= ~(1 << 18);
	// sg_write32(vdev->intc_cfg_reg, 0xc, intc_mask);
}

static int notify_host(struct veth_dev *vdev)
{
	u32 data;
#ifdef VETH_IRQ
	if (atomic_read(&vdev->link)) {
		data = 0x4;
		sg_write32(vdev->top_misc_reg, TOP_MISC_GP_REG14_SET_OFFSET, data);
	}
	// sg_enable_eth_irq(vdev);
#else
	if (atomic_read(&vdev->link)) {
		data = 0x1;
		sg_write32(vdev->shm_mem, 0x60, data);
	}
#endif
	return NETDEV_TX_OK;
}

static int veth_open(struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int err;

	// intr_clear(vdev);
	if (devm_request_irq(&vdev->pdev->dev, vdev->rx_irq, veth_irq, 0, "veth", vdev)) {
		pr_err("request rx irq failed!\n");
		return 1;
	}
	// sg_enable_eth_irq(vdev);
	disable_irq(vdev->rx_irq);
	atomic_set(&vdev->link, false);
	err = set_ready_flag(vdev);
	if (err) {
		pr_err("set ready falg failed!\n");
		return 1;
	}
	netdev_reset_queue(ndev);
	netif_start_queue(ndev);
	napi_enable(&vdev->napi);

	return 0;
}

static int veth_close(struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);

	napi_disable(&vdev->napi);
	netif_stop_queue(ndev);
	return 0;
}

static unsigned int queue_used(u32 read, u32 write, u32 len)
{
	unsigned int used;

	if (write >= read)
		used = write - read;
	else
		used = len - (read - write);

	return used;
}

static unsigned int queue_free(u32 read, u32 write, u32 len)
{
	return len - queue_used(read, write, len) - 1;
}

static unsigned int __enqueue(void *queue, unsigned int write, void *data, int len)
{
	unsigned int tmp;

	tmp = min_t(unsigned int, len, QUENE_LEN_CPU - write);
	memcpy_toio((u8 *)queue + write, data, tmp);
	memcpy_toio((u8 *)queue, (u8 *)data + tmp, len - tmp);

	write = (write + len) % QUENE_LEN_CPU;

	return write;
}


static netdev_tx_t veth_xmit_cpu(struct sk_buff *skb, struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int err;
	int ret;
	u32 write, read, write_tmp;
	// struct veth_addr veth_node;
	// u64 paddr;
	unsigned int total_len = round_up(skb->len + sizeof(u32), PT_ALIGN);

	if (!atomic_read(&vdev->link)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	write = sg_read32(vdev->shm_mem, 0x84);
	read = sg_read32(vdev->shm_mem, 0x80);

	if (queue_free(read, write, QUENE_LEN_CPU) < total_len) {
		// pr_info("quene full!\n");
		notify_host(vdev);
		return NETDEV_TX_BUSY;
	}

	write_tmp = __enqueue(vdev->tx_mem, write, (u8 *)(&(skb->len)), sizeof(u32));
	__enqueue(vdev->tx_mem, write_tmp, skb->data, skb->len);

	write = (write + total_len) % QUENE_LEN_CPU;
	sg_write32(vdev->shm_mem, 0x84, write);

	ret = notify_host(vdev);
	if (ret == NETDEV_TX_BUSY)
		return ret;

	++ndev->stats.tx_packets;
	ndev->stats.tx_bytes += err;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static netdev_tx_t __maybe_unused veth_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int err;
	int ret;
	u32 write, read;
	struct veth_addr veth_node;
	u64 paddr;

	if (!atomic_read(&vdev->link)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	write = sg_read32(vdev->shm_mem, 0x84);
	read = sg_read32(vdev->shm_mem, 0x80);

	if (queue_free(read, write, sizeof(struct veth_addr) * 100) == 0) {
		pr_info("quene full!\n");
		notify_host(vdev);
		return NETDEV_TX_BUSY;
	}

	memcpy_fromio(&veth_node, vaddr_tx + write, sizeof(struct veth_addr));

	paddr = virt_to_phys(skb->data);
	// ret = sg_eth_memcpy_s2d(vdev, veth_node.paddr, paddr, skb->len);
	// if (ret != 0)
	// 	return ret;

	write = (write + sizeof(struct veth_addr)) % (sizeof(struct veth_addr) * 100);
	sg_write32(vdev->shm_mem, 0x84, write);

	ret = notify_host(vdev);
	if (ret == NETDEV_TX_BUSY)
		return ret;

	++ndev->stats.tx_packets;
	ndev->stats.tx_bytes += err;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops veth_ops = {
	.ndo_open = veth_open,
	.ndo_stop = veth_close,
#ifdef USE_CDMA
	.ndo_start_xmit = veth_xmit,
#else
	.ndo_start_xmit = veth_xmit_cpu,
#endif
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	// .ndo_change_mtu = eth_change_mtu,
};

static int __maybe_unused veth_rx(struct veth_dev *vdev, int limit)
{
	struct sk_buff *skb;
	struct sk_buff *skb_new;
	int read, write;
	struct veth_addr veth_node;
	int skb_len = ETH_MTU + 0xd;
	void *skb_vaddr;
	u64 skb_paddr;
	int count = 0;
	struct napi_struct *napi;
	struct net_device *ndev;

	ndev = vdev->ndev;
	napi = &vdev->napi;

	if (!atomic_read(&vdev->link)) {
		pr_info("veth rx not link!\n");
		return count;
	}

	while (count < limit) {
		read = sg_read32(vdev->shm_mem, 0x90);
		write = sg_read32(vdev->shm_mem, 0x94);
		// pr_info("read: 0x%x, write: 0x%x\n", read, write);
		if (read == write) {
			napi_complete(napi);
			break;
		}

		memcpy_fromio((u8 *)(&veth_node), vaddr_rx + read, sizeof(struct veth_addr));

		dma_sync_single_range_for_cpu(&vdev->pdev->dev, veth_node.paddr, 0, skb_len, DMA_FROM_DEVICE);
		skb = veth_node.skb;

		skb->protocol  = eth_type_trans(skb, napi->dev);
		// skb->dev       = info->ndev;
		skb->ip_summed = CHECKSUM_NONE;
		napi_gro_receive(napi, skb);
		napi->dev->stats.rx_packets++;
		napi->dev->stats.rx_bytes += skb->len;

		skb_new = netdev_alloc_skb(ndev, skb_len + NET_IP_ALIGN);
		if (!skb_new)
			return count;

		skb_reserve(skb_new, NET_IP_ALIGN);  // align IP on 16B boundary
		skb_reset_mac_header(skb_new);
		skb_vaddr = skb_put(skb_new, skb_len);
		skb_paddr = virt_to_phys(skb_vaddr);
		veth_node.paddr = skb_paddr;
		veth_node.skb = skb_new;

		memcpy_toio(vaddr_rx + read, (u8 *)(&veth_node), sizeof(struct veth_addr));
		read = (read + sizeof(struct veth_addr)) % (sizeof(struct veth_addr) * 100);
		sg_write32(vdev->shm_mem, 0x90, read);
		count++;
	}

	return count;
}

static unsigned int __dequeue(void *queue, unsigned int tail, void *data, int len)
{
	unsigned int tmp;

	tmp = min_t(unsigned int, len, QUENE_LEN_CPU - tail);
	memcpy_fromio(data, (u8 *)queue + tail, tmp);
	memcpy_fromio((u8 *)data + tmp, queue, len - tmp);

	tail = (tail + len) % QUENE_LEN_CPU;

	return tail;
}

static int veth_rx_cpu(struct veth_dev *vdev, int limit)
{
	struct sk_buff *skb;
	int read, write;
	int count = 0;
	struct napi_struct *napi;
	struct net_device *ndev;
	u32 skb_len;
	unsigned int total_len;
	u32 read_temp;

	ndev = vdev->ndev;
	napi = &vdev->napi;

	if (!atomic_read(&vdev->link)) {
		pr_info("veth rx not link!\n");
		return count;
	}

	while (count < limit) {
		read = sg_read32(vdev->shm_mem, 0x90);
		write = sg_read32(vdev->shm_mem, 0x94);

		if (read == write) {
			napi_complete(napi);
			break;
		}

		skb_len = ioread32(vdev->rx_mem + read);
		total_len = round_up(skb_len + sizeof(u32), PT_ALIGN);
		// pr_info("read: 0x%x, write: 0x%x, len: 0x%x\n", read, write, skb_len);
		if (queue_used(read, write, QUENE_LEN_CPU) < total_len)
			return -ENOMEM;

		skb = netdev_alloc_skb(ndev, skb_len + NET_IP_ALIGN);
		if (!skb) {
			// pr_err("allocate skb failed with package length %u\n", pkg_len);
			++ndev->stats.rx_dropped;
			continue;
		}

		skb_reserve(skb, NET_IP_ALIGN);

		read_temp = (read + sizeof(u32)) % QUENE_LEN_CPU;
		__dequeue(vdev->rx_mem, read_temp, skb_put(skb, skb_len), skb_len);

		/* free space as soon as possible */
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&vdev->napi, skb);
		++ndev->stats.rx_packets;
		ndev->stats.rx_bytes += skb->len;

		read = (read + total_len) % QUENE_LEN_CPU;
		sg_write32(vdev->shm_mem, 0x90, read);
		count++;
	}

	return count;
}

static int veth_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct veth_dev *vdev;
	int err;

	vdev = container_of(napi, struct veth_dev, napi);
#ifdef USE_CDMA
	err = veth_rx(vdev, budget);
#else
	err = veth_rx_cpu(vdev, budget);
#endif
	return err;
}

static void bm1684x_veth_alloc_addr(struct veth_dev *vdev)
{
	// int skb_len = ETH_MTU + 0xd;
	// // struct eth_dev_info *veth = &bmdi->vir_eth;
	// struct sk_buff *skb = NULL;
	// void *skb_vaddr;
	// u64 skb_paddr;
	// struct veth_addr veth_node;
	// int quene_num = 100;
	// int i;
	// struct net_device *ndev;

	// ndev = vdev->ndev;
	sg_write32(vdev->shm_mem, 0x90, 0);
	sg_write32(vdev->shm_mem, 0x94, 0);

	// for (i = 0; i < quene_num; i++) {
	// 	skb = netdev_alloc_skb(ndev, skb_len + NET_IP_ALIGN);

	// 	skb_reserve(skb, NET_IP_ALIGN);  // align IP on 16B boundary
	// 	skb_reset_mac_header(skb);
	// 	skb_vaddr = skb_put(skb, skb_len);
	// 	skb_paddr = virt_to_phys(skb_vaddr);
	// 	veth_node.paddr = skb_paddr;
	// 	veth_node.skb = skb;
	// 	memcpy_toio(vaddr_rx + i*sizeof(struct veth_addr), (u8 *)(&veth_node), sizeof(struct veth_addr));
	// 	// veth_memcpy_toio(bmdi, i*sizeof(struct veth_addr), (u8 *)(&veth_node), sizeof(struct veth_addr));
	// 	pr_info("skb_paddr: 0x%llx\n", veth_node.paddr);
	// }
}

static void net_state_work(struct work_struct *worker)
{
	struct veth_dev *vdev;
	struct net_device *ndev;
	struct device *dev;
	int val;

	vdev = container_of(worker, struct veth_dev, net_state_worker);
	ndev = vdev->ndev;
	dev = &vdev->pdev->dev;
	bm1684x_veth_alloc_addr(vdev);

	while (sg_read32(vdev->shm_mem, SHM_HANDSHAKE_OFFSET)) {
		msleep(100);
	}

	sg_write32(vdev->shm_mem, SHM_HANDSHAKE_OFFSET, DEVICE_READY_FLAG);

	while (sg_read32(vdev->shm_mem, SHM_HANDSHAKE_OFFSET) != HOST_READY_FLAG)
		mdelay(10);

	/* i am ready again */
	sg_write32(vdev->shm_mem, SHM_HANDSHAKE_OFFSET, DEVICE_READY_FLAG);
	spin_lock_init(&vdev->lock_cdma);
	atomic_set(&vdev->link, true);
	netif_carrier_on(ndev);
	enable_irq(vdev->rx_irq);
	pr_info("connect success!\n");

	while(1) {
		msleep(1);

		val = sg_read32(vdev->shm_mem, 0x64);
		if (val == 1) {
			sg_write32(vdev->shm_mem, 0x64, 0);
			napi_schedule(&vdev->napi);
		}
	}
}

static int set_ready_flag(struct veth_dev *vdev)
{
	INIT_WORK(&vdev->net_state_worker, net_state_work);
	schedule_work(&vdev->net_state_worker);

	return 0;
}

static void unset_ready_flag(struct veth_dev *vdev)
{
	sg_write32(vdev->shm_mem, SHM_HANDSHAKE_OFFSET, 0);
}

static int sg_veth_get_resource(struct platform_device *pdev, struct veth_dev *vdev)
{
	int err;

	vdev->shm_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "shm_reg");
	if (!vdev->shm_res) {
		pr_err("cannot get resource - shm reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->shm_mem = ioremap(vdev->shm_res->start, vdev->shm_res->end - vdev->shm_res->start);
	if (!vdev->shm_mem) {
		pr_err("map shm cfg reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->rx_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rx_reg");
	if (!vdev->rx_res) {
		pr_err("cannot get resource - rx res base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->rx_mem = ioremap(vdev->rx_res->start, vdev->rx_res->end - vdev->rx_res->start);
	if (!vdev->rx_mem) {
		pr_err("map top misc reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->tx_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tx_reg");
	if (!vdev->tx_res) {
		pr_err("cannot get resource - tx reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->tx_mem = ioremap(vdev->tx_res->start, vdev->tx_res->end - vdev->tx_res->start);
	if (!vdev->tx_mem) {
		pr_err("map cdma tx reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->irq_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "irq_reg");
	if (!vdev->irq_res) {
		pr_err("cannot get resource - irq reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->irq_mem = ioremap(vdev->irq_res->start, vdev->irq_res->end - vdev->irq_res->start);
	if (!vdev->irq_mem) {
		pr_err("map irq reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->rx_irq = platform_get_irq_byname(pdev, "rx");
	if (vdev->rx_irq < 0) {
		pr_err("no rx interrupt resource!\n");
		err = -ENOMEM;
		return err;
	}

	return 0;
}

#define PDE_DATA(inode) ((inode)->i_private)
static ssize_t proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char value_str[16];
	struct veth_dev *vdev = PDE_DATA(file_inode(file));
    size_t len;
	u32 ip = sg_read32(vdev->shm_mem, VETH_IPADDRESS_REG);

	len = snprintf(value_str, sizeof(value_str), "0x%X\n", ip);

    if (*ppos >= len)
        return 0;

    if (count > len - *ppos) {
        count = len - *ppos;
    }

    if (copy_to_user(buf, value_str + *ppos, count)) {
        return -EFAULT;
    }

    *ppos += count;

    return count;
}

static const struct proc_ops sgdrv_vethip_file_ops = {
	.proc_read = proc_read,
};

static int sg_veth_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct veth_dev *vdev;
	struct net_device *ndev;
	int err;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "sg2260, sgveth");
	if (!of_device_is_available(node)) {
		pr_err("veth driver node is disable!\n");
		err = -ENODEV;
		return err;
	}

	dev = &pdev->dev;
	dma_set_mask(dev, DMA_BIT_MASK(64));
	ndev = alloc_etherdev(sizeof(struct veth_dev));
	if (!ndev) {
		pr_err("cannot allocate device instance !\n");
		return -ENOMEM;
	}

	ether_setup(ndev);
	strncpy(ndev->name, "veth%d", IFNAMSIZ);
	eth_hw_addr_random(ndev);

	vdev = netdev_priv(ndev);
	vdev->ndev = ndev;

	err = sg_veth_get_resource(pdev, vdev);
	if (err != 0)
		goto err_free_netdev;

	vdev->pdev = pdev;
	platform_set_drvdata(pdev, ndev);

	ndev->netdev_ops = &veth_ops;
	ndev->irq = vdev->rx_irq;
	ndev->mtu = VETH_DEFAULT_MTU;

	// netif_napi_add(ndev, &vdev->napi, veth_napi_poll_rx, NAPI_POLL_WEIGHT);
	netif_napi_add(ndev, &vdev->napi, veth_napi_poll_rx);
	err = register_netdev(ndev);
	if (err) {
		pr_err("register net device failed!\n");
		goto err_free_netdev;
	}

	sgdrv_proc_dir = proc_mkdir(debug_node_name, NULL);
	if (!sgdrv_proc_dir)
		return -ENOMEM;

	sgdrv_vethip = proc_create_data("vethip", 0666, sgdrv_proc_dir, &sgdrv_vethip_file_ops, (void *)vdev);
	if (!sgdrv_vethip) {
		proc_remove(sgdrv_vethip);
		return -ENOMEM;
	}

	pr_info("veth probe over!\n");
	return 0;

err_free_netdev:
	free_netdev(ndev);
	return err;
}

static void sg_veth_remove(struct platform_device *pdev)
{
	struct veth_dev *vdev;
	struct net_device *ndev;

	ndev = platform_get_drvdata(pdev);

	netif_carrier_off(ndev);
	if (!ndev) {
		pr_err("sg_veth_remove failed because ndev is null.\n");
		return;
	}

	vdev = netdev_priv(ndev);
	disable_irq(vdev->rx_irq);

	unset_ready_flag(vdev);
	return;
}

static const struct of_device_id sg_veth_match[] = {
	{.compatible = "sg2260, sgveth"},
	{},
};

static struct platform_driver sg_veth_driver = {
	.probe = sg_veth_probe,
	.remove = sg_veth_remove,
	.driver = {
		.name = "sgveth",
		.of_match_table = sg_veth_match,
	},
};

module_platform_driver(sg_veth_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dong Yang <dong.yang@sophgon.com>");
MODULE_DESCRIPTION("Sophgon Virtual Ethernet Driver over PCIe");
