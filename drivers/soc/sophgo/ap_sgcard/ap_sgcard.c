// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include "sgdrv.h"
#include "../pcie_ep/ap_pcie_ep.h"
#include "../c2c_rc/c2c_rc.h"

void arch_wb_cache_pmem(void *addr, size_t size);
void arch_invalidate_pmem(void *addr, size_t size);
int probe_sgcard(struct sophgo_pcie_vfun *sg_vfun);

struct sg_card;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 1)
	#define compat_class_create(model, name) class_create(name)
#else
	#define compat_class_create(model, name) class_create(model, name)
#endif

static int debug_enable;
static int record_response;

#define DBG_MSG(fmt, args...) \
do { \
	if (debug_enable == 1) \
		pr_info("[sg]%s:" fmt, __func__, ##args); \
} while (0)

#define MSGFIFO_HEAD_SIZE	(1 * 1024 * 1024)
#define PORT_MAX	256
#define DRV_NAME	"sgdrv"

#define RUUNING_STAGES	0x1c4
#define DRIVER_EXECED	0x5

#define WAKE_UP_ALL_STREAM	(0xffffffffffffffff)

#define MAX_HOST_REQUEST_SIZE (8 * 1024 * 1024)


const char *request_response_type[] = {
	[ERROR_REQUEST_RESPONSE] = "error request response type",
	[STREAM_CREATE_REQUEST] = "stream create request",
	[STREAM_CREATE_RESPONSE] = "stream create response",
	[STREAM_DESTROY_REQUEST] = "stream destroy request",
	[STREAM_DESTROY_RESPONSE] = "stream destroy response",
	[MALLOC_DEVICE_MEM_REQUEST] = "malloc device memory request",
	[MALLOC_DEVICE_MEM_RESPONSE] = "malloc device memory response",
	[FREE_DEVICE_MEM_REQUEST] = "free device memory request",
	[FREE_DEVICE_MEM_RESPONSE] = "free device memory response",
	[CALLBACK_REQUEST] = "callback request",
	[CALLBACK_RESPONSE] = "callback response",
	[CALLBACK_RELEASE] = "callback release",
	[EVENT_CREATE_REQUEST] = "event create request",
	[EVENT_CREATE_RESPONSE] = "event create response",
	[TASK_CREATE_REQUEST] = "task create request",
	[TASK_CREATE_RESPONSE] = "task create response",
	[EVENT_TRIGGERED] = "event triggered",
	[TASK_DONE_RESPONSE] = "task done response",
	[BTM_TASK_DONE_RESPONSE] = "btm task done response",
	[TASK_ERROR_RESPONSE] = "task errror response",
	[FORCE_QUIT_REQUEST] = "force quit request",
	[SETUP_C2C_REQUEST] = "set up c2c request",
	[SETUP_C2C_RESPONSE] = "set up c2c response",
};

const char *task_type[] = {
	[ERROR_TASK_TYPE] = "error task type",
	[TASK_S2D] = "task s2d",
	[TASK_D2S] = "task d2s",
	[TASK_D2D] = "task d2d",
	[LAUNCH_KERNEL] = "launch kernel",
	[TRIGGER_TASK] = "trigger task",
};

struct cacheline_align_circ_buf {
	union {
		uint64_t head;
		uint64_t head_align[8];
	};

	union {
		uint64_t tail;
		uint64_t tail_align[8];
	};
	uint64_t phy_addr;
	char *buf;
	int (*circ_buf_read)(void *circ_buf, void *user_buf, uint64_t size);
	int (*circ_buf_write)(void *circ_buf, void *user_buf, uint64_t size);
	uint64_t align[4];
};

struct clr_irq {
	uint64_t phy_addr;
	void __iomem *clr_irq_va;
	uint64_t clr_irq_data;
};

struct v_channel_info {
	struct list_head rp_list;
	int port_cnt;
	uint64_t int_rcv_cnt;
};
struct v_channel {
	struct list_head port_list;
	struct cacheline_align_circ_buf *tx_buf;
	struct cacheline_align_circ_buf *rx_buf;
	struct vector_info tx_send_irq;
	struct clr_irq rx_clean_irq;
	uint64_t channel_index;
	uint64_t irq;
	struct mutex tx_lock;
	spinlock_t port_lock;
	char name[16];
	struct v_channel_info channel_info;
	int (*channel_irq_handel)(struct sg_card *card, struct v_channel *channle);
	struct delayed_work channel_delayed_work;
};

struct v_port_info {
	uint64_t rcv_bytes;
	uint64_t send_bytes;
	int all_msg_cnt[20];
	int last_tx_msg_type;
	int last_rx_msg_type;
};
struct v_port {
	struct list_head list;
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	dev_t devno;
	char name[32];
	struct v_channel *parent_channel;
	struct sg_card *card;
	char *write_buf;
	struct circ_buf port_rx_buf;
	uint64_t stream_id;
	atomic_t cnt_available;
	atomic_t wake_up_task_cnt;
	wait_queue_head_t read_available;
	atomic_t cnt_opened;
	struct delayed_work destroy_stream_work;
	struct v_port_info port_info;
};

struct wake_up_stream_port {
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	dev_t devno;
	char name[32];
	struct sg_card *card;
	wait_queue_head_t resource_available;
	atomic_t cnt_opened;
};

struct sg_card_cdev_info {
	struct device *dev;
	struct class *sg_class;
	dev_t devno;
	char devno_map[PORT_MAX];
};

struct host_channel_verify {
	void *verify_addr;
	uint64_t head_verify_val;
	uint64_t verify_enable;
};

struct sg_card {
	uint64_t addr_start;
	void __iomem *membase;
	void __iomem *top_base;
	void __iomem *config_file;
	struct proc_dir_entry *tpurt_dir;
	struct proc_dir_entry *tpurt_proc;
	uint32_t *tx_pool_index;
	uint32_t *rx_pool_index;
	uint32_t host_channel_count;
	uint32_t tpu_channel_count;
	uint32_t media_channel_count;
	uint32_t channel_count;
	uint32_t pool_size;
	uint32_t share_memory_type;
	uint64_t host_channel_irq_base;
	uint64_t tpu_channel_irq_base;
	uint64_t media_channel_irq_base;
	struct v_channel *channel;
	struct sg_card_cdev_info cdev_info;
	const struct sophgo_pcie_vfun *pcie_vfun;
	struct wake_up_stream_port wake_up_stream_port;
	atomic_t c2c_kernel_token;
	struct host_channel_verify verify;
	uint64_t last_reqid;
};

static struct sg_card *g_card;

static void set_card(struct sg_card *card)
{
	g_card = card;
}

static struct sg_card *get_card(void)
{
	return g_card;
}

// __weak struct vector_info *sophgo_ep_alloc_vector(int pcie_id, int vector_id)
// {
//	pr_err("soc mode not need allo vector from pcie, please check your dts\n");

//	return NULL;
// }

static int destroy_stream_cdev_by_fd(struct v_port *port);
static int create_stream_cdev(struct sg_card *card,
			      struct sg_stream_info *stream_info);

__attribute__((unused)) static uint64_t read_arch_timer(void)
{
#ifdef __aarch64__
	uint64_t cnt = 0;

	__asm__ __volatile__ ("isb\n\t"
				"mrs %0, cntvct_el0\n\t"
		: "=r" (cnt));

	return cnt * 20;
#endif

#ifdef __riscv_xlen
	unsigned long n;

	__asm__ __volatile__("rdtime %0" : "=r"(n));
	return n * 20;
#endif

#ifdef __x86_64__
	uint64_t cnt = 0;

	return cnt;
#endif

}

inline int set_card_devno_map(struct sg_card *card, int devno_index)
{
	card->cdev_info.devno_map[devno_index] = 1;

	return devno_index;
}

inline int clr_card_devno_map(struct sg_card *card, int devno_index)
{
	card->cdev_info.devno_map[devno_index] = 0;

	return devno_index;
}

inline int find_and_set_devno_map(struct sg_card *card)
{
	int i;

	for (i = 0; i < PORT_MAX; i++) {
		if (card->cdev_info.devno_map[i] == 0) {
			set_card_devno_map(card, i);
			return i;
		}
	}

	return -ENOSPC;
}

__attribute__((unused)) static int copy_from_circbuf(char *to, struct circ_buf *buf, int length,
			     uint64_t pool_size)
{
	int c = 0;

	while (1) {
		c = CIRC_CNT(buf->head, buf->tail, pool_size);
		if (c >= length)
			break;
	}

	while (1) {
		c = CIRC_CNT_TO_END(buf->head, buf->tail, pool_size);
		if (length < c)
			c = length;

		memcpy_fromio(to, buf->buf + buf->tail, c);
		buf->tail = (buf->tail + c) & (pool_size - 1);
		to += c;
		length -= c;
		if (length == 0)
			break;
	}

	return length;
}

static int copy_from_circbuf_not_change_index(char *to, struct cacheline_align_circ_buf *buf,
					      int length, uint64_t pool_size)
{
	uint64_t tail = buf->tail;
	uint64_t head = buf->head;
	uint64_t c;

	buf->circ_buf_read(&buf->head, &head, sizeof(head));
	buf->circ_buf_read(&buf->tail, &tail, sizeof(tail));

	while (1) {
		c = CIRC_CNT_TO_END(head, tail, pool_size);
		if (length < c)
			c = length;

		DBG_MSG("circ cnt to end:%llu\n", c);
		buf->circ_buf_read(buf->buf + tail, to, c);
		tail = (tail + c) & (pool_size - 1);
		to += c;
		length -= c;
		if (length == 0)
			break;
	}

	return length;
}

static int copy_to_circbuf(struct circ_buf *rx_buf, char *from, int length, uint64_t pool_size)
{
	int c = 0;

	while (1) {
		c = CIRC_SPACE(rx_buf->head, rx_buf->tail, pool_size);
		if (c >= length)
			break;
	}

	while (1) {
		c = CIRC_SPACE_TO_END(rx_buf->head, rx_buf->tail, pool_size);
		if (length < c)
			c = length;

		memcpy_toio(rx_buf->buf + rx_buf->head, from, c);
		smp_mb();
		rx_buf->head = (rx_buf->head + c) & (pool_size - 1);
		from += c;
		length -= c;
		if (length == 0)
			break;
	}

	return length;
}

static inline int channel_clr_irq(struct v_channel *channel)
{
	uint32_t *addr = channel->rx_clean_irq.clr_irq_va;
	uint32_t data = channel->rx_clean_irq.clr_irq_data;

	DBG_MSG("channel clr irq:addr:0x%px, data:0x%x\n", addr, data);

	iowrite32(data, addr);

	return 0;
}

static int channel_tx_send_irq(struct v_channel *channel)
{
	void __iomem *addr = (void __iomem *)channel->tx_send_irq.msi_va;
	uint32_t data = channel->tx_send_irq.msi_data;

	if (channel->channel_index != CHANNEL_HOST) {
		DBG_MSG("sikp send irq\n");
		return 0;
	}

	DBG_MSG("channel send irq:addr:0x%px, data:0x%x\n", addr, data);

	writel(data, addr);

	return 0;
}

static uint64_t host_int_cnt;

static void host_int_work_func (struct work_struct *p_work)
{
	struct v_channel *channel = container_of(p_work, struct v_channel, channel_delayed_work.work);

	iowrite32(0x1, channel->rx_clean_irq.clr_irq_va);
}

static uint64_t calculate_verify(uint8_t *data, size_t len)
{
	uint64_t bip64 = 0;
	uint32_t verify_len = len / sizeof(uint64_t);
	uint64_t *data64 = (uint64_t *)data;

	for (size_t i = 0; i < verify_len; i++)
		bip64 ^= data64[i];

	return bip64;
}

static int verify_data(struct sg_card *card, void *data, uint64_t len, uint64_t verify_val)
{
	uint64_t cal_verify_val;

	if (card->verify.verify_enable) {
		cal_verify_val = calculate_verify((uint8_t *)data, len);
		if (cal_verify_val != verify_val) {
			pr_err("error verify, get head_verify_val:0x%llx, calc_val:0x%llx, try again\n",
				verify_val, cal_verify_val);
			return -1;
		}
	}

	return 0;
}

static int host_int(struct sg_card *card, struct v_channel *channel)
{
	struct cacheline_align_circ_buf *rx_buf = NULL;
	struct list_head *port_list;
	struct v_port *port = NULL;
	struct circ_buf *port_rx_buf = NULL;
	struct host_request_action request_action;
	int c;
	int from_len;
	int to_len;
	int length;
	unsigned long flags;
	struct timespec64 ts;
	struct task_head task_head;
	int i = 0;
	uint64_t head;
	uint64_t tail;
	int fifo_empty_cnt = 0;
	int ret;

	DBG_MSG("enter host int:0x%llx\n", host_int_cnt++);

	rx_buf = channel->rx_buf;

	while (1) {
		rx_buf->circ_buf_read(&rx_buf->head, &head, sizeof(head));
		rx_buf->circ_buf_read(&rx_buf->tail, &tail, sizeof(tail));
		DBG_MSG("host int [ch:0x%llx] head:0x%llx, tail:0x%llx\n", channel->channel_index, head, tail);
		c = CIRC_CNT(head, tail, card->pool_size);

		if (c == 0 && fifo_empty_cnt != 6) {
			fifo_empty_cnt++;
			continue;
		} else if (c == 0 && fifo_empty_cnt == 6) {
			break;
		}

		fifo_empty_cnt = 0;

		if (c < sizeof(request_action)) {
			pr_err("warning error host requestion action len\n");
			break;
		}
		DBG_MSG("int rcv %d bytes\n", c);

		copy_from_circbuf_not_change_index((char *)&request_action, rx_buf,
							sizeof(request_action), card->pool_size);
		ret = verify_data(card, &request_action, sizeof(struct host_request_action) -
				  sizeof(struct time_stamp), request_action.time.head_verify_val);
		if (ret) {
			pr_err("verify request head error, try again\n");
			pr_err("head not match, head:0x%llx, tail:0x%llx last id:0x%llx, get id:0x%llx, try again\n",
				head, tail, card->last_reqid, request_action.request_id);
			for (i = 0; i < sizeof(request_action) / sizeof(uint64_t); i++)
				pr_err("offset:%d data:0x%llx\n", i, ((uint64_t *)(&request_action))[i]);
			schedule_delayed_work(&channel->channel_delayed_work, 1);
			return 0;
		}

		if (request_action.request_id <= card->last_reqid || (request_action.request_id - card->last_reqid) > 10) {
			pr_err("id not match, head:0x%llx, tail:0x%llx last id:0x%llx, get id:0x%llx, try again\n",
				head, tail, card->last_reqid, request_action.request_id);
			for (i = 0; i < sizeof(request_action) / sizeof(uint64_t); i++)
				pr_err("offset:%d data:0x%llx\n", i, ((uint64_t *)(&request_action))[i]);

			schedule_delayed_work(&channel->channel_delayed_work, 1);
			return 0;
		}
		ktime_get_real_ts64(&ts);
		DBG_MSG("host int [ch:0x%llx] request_id:0x%llx, request_type:0x%x\n",
			channel->channel_index, request_action.request_id, request_action.type);

		if (request_action.type == ERROR_REQUEST_RESPONSE || request_action.type > SETUP_C2C_REQUEST) {
			pr_err("error type host request type is %u, try again\n", request_action.type);
			for (i = 0; i < sizeof(request_action) / sizeof(uint64_t); i++)
				pr_err("offset:%d data:0x%llx\n", i, ((uint64_t *)(&request_action))[i]);
			schedule_delayed_work(&channel->channel_delayed_work, 1);

			return 0;
		}
		//request_action.time.kr_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);

		if (request_action.type < FREE_DEVICE_MEM_RESPONSE || request_action.type == SETUP_C2C_REQUEST ||
		    request_action.type == FORCE_QUIT_REQUEST) {
			port_list = channel->port_list.next;
			port = container_of(port_list, struct v_port, list);
			DBG_MSG("channel 0 fd\n");
		} else {
			spin_lock_irqsave(&channel->port_lock, flags);
			list_for_each_entry(port, &channel->port_list, list) {
				DBG_MSG("port->stream_id:0x%llx, request_stream_id:0x%llx\n",
					port->stream_id, request_action.stream_id);
				if (port->stream_id == request_action.stream_id)
					break;
			}
			spin_unlock_irqrestore(&channel->port_lock, flags);

			if (list_entry_is_head(port, &channel->port_list, list)) {
				pr_err("request_id:0x%llx, request_type:0x%x, no stream id:0x%llx match\n",
					request_action.request_id, request_action.type, request_action.stream_id);
				pr_err("[error stream id]:current tail:0x%llx\n", tail);
				schedule_delayed_work(&channel->channel_delayed_work, 1);

				return 0;

			}

			//TODO: red-black tree
		}
		port_rx_buf = &port->port_rx_buf;
		port->port_info.all_msg_cnt[request_action.type]++;
		port->port_info.last_rx_msg_type = request_action.type;

		if (request_action.type == TASK_CREATE_REQUEST || request_action.type == MALLOC_DEVICE_MEM_REQUEST
		    || request_action.type == FREE_DEVICE_MEM_REQUEST) {
			length = request_action.task_size;
			DBG_MSG("request id:0x%llx, task size:0x%x\n", request_action.request_id,
				 request_action.task_size);
			rx_buf->circ_buf_read(&rx_buf->head, &head, sizeof(head));
			rx_buf->circ_buf_read(&rx_buf->tail, &tail, sizeof(tail));
			c = CIRC_CNT(head, tail, card->pool_size);
			if (c < length + sizeof(request_action)) {
				pr_err("warning, task size is not match\n");
				pr_err("request id:0x%llx, type:0x%x, bring bufer size:0x%x but want size:0x%lx\n",
					request_action.request_id, request_action.type, c,
					length + sizeof(request_action));
				pr_err("host rx buf head:0x%llx, tail:0x%llx\n", head, tail);
				pr_err("port rx buf head:0x%x, tail:0x%x\n", port_rx_buf->head, port_rx_buf->tail);
				break;
			}

			if (card->verify.verify_enable) {
				copy_from_circbuf_not_change_index((char *)card->verify.verify_addr, rx_buf,
							sizeof(request_action) + request_action.task_size, card->pool_size);
				if (verify_data(card, card->verify.verify_addr + sizeof(struct host_request_action),
						length, request_action.time.body_verify_val)) {
					pr_err("task body error verify, try again lenght=%d\n", length);
					schedule_delayed_work(&channel->channel_delayed_work, 1);
					return 0;
				}
			}

			c = CIRC_SPACE(port_rx_buf->head, port_rx_buf->tail, card->pool_size);
			if (c < length + sizeof(request_action)) {
				pr_err("task body lenght not match head:0x%llx, tail:0x%llx length=%d\n", head, tail, length);
				schedule_delayed_work(&channel->channel_delayed_work, 1);
				break;
			}
			DBG_MSG("copy [%s]-0x%llx to %s, port buf head:0x%x, tail:0x%x\n",
				request_response_type[request_action.type], request_action.request_id, port->name,
				port_rx_buf->head, port_rx_buf->tail);
			copy_to_circbuf(port_rx_buf, (char *)&request_action, sizeof(request_action), card->pool_size);
			tail = (tail + sizeof(request_action)) & (card->pool_size - 1);
			rx_buf->circ_buf_write(&rx_buf->tail, &tail, sizeof(tail));

			if (request_action.type == TASK_CREATE_REQUEST) {
				copy_from_circbuf_not_change_index((char *)&task_head, rx_buf,
							sizeof(task_head), card->pool_size);
				if (task_head.task_type == LAUNCH_KERNEL && task_head.kernel_type == C2C_KERNEL)
					task_head.task_token = atomic_add_return(1, &card->c2c_kernel_token);
				DBG_MSG("copy task %s to %s, task body size:0x%llx\n", task_type[task_head.task_type],
					 port->name, task_head.task_body_size);
				copy_to_circbuf(port_rx_buf, (char *)&task_head, sizeof(task_head), card->pool_size);
				tail = (tail + sizeof(task_head)) & (card->pool_size - 1);
				rx_buf->circ_buf_write(&rx_buf->tail, &tail, sizeof(tail));
				length -= sizeof(task_head);
			}

			while (1) {
				rx_buf->circ_buf_read(&rx_buf->head, &head, sizeof(head));
				rx_buf->circ_buf_read(&rx_buf->tail, &tail, sizeof(tail));
				from_len = CIRC_CNT_TO_END(head, tail, card->pool_size);
				to_len = CIRC_SPACE_TO_END(port_rx_buf->head, port_rx_buf->tail, card->pool_size);
				c = min(from_len, to_len);
				if (length < c)
					c = length;

				rx_buf->circ_buf_read(rx_buf->buf + tail, port_rx_buf->buf + port_rx_buf->head, c);
				smp_mb();
				port_rx_buf->head = (port_rx_buf->head + c) & (card->pool_size - 1);
				tail = (tail + c) & (card->pool_size - 1);
				rx_buf->circ_buf_write(&rx_buf->tail, &tail, sizeof(tail));
				length -= c;
				if (length == 0)
					break;
			}
			channel->channel_info.int_rcv_cnt += (sizeof(request_action) + request_action.task_size);
			atomic_add(sizeof(request_action) + request_action.task_size, &port->cnt_available);
			wake_up_all(&port->read_available);
		} else {
			DBG_MSG("only copy [%s]-0x%llx to %s\n", request_response_type[request_action.type],
				 request_action.request_id, port->name);
			c = CIRC_SPACE(port_rx_buf->head, port_rx_buf->tail, card->pool_size);
			if (c < sizeof(request_action)) {
				schedule_delayed_work(&channel->channel_delayed_work, 1);
				break;
			}

			copy_to_circbuf(port_rx_buf, (char *)&request_action, sizeof(request_action), card->pool_size);
			tail = (tail + sizeof(request_action)) & (card->pool_size - 1);
			rx_buf->circ_buf_write(&rx_buf->tail, &tail, sizeof(tail));
			channel->channel_info.int_rcv_cnt += (sizeof(request_action));
			atomic_add(sizeof(request_action), &port->cnt_available);
			wake_up_all(&port->read_available);
		}
		card->last_reqid = request_action.request_id;
	}

	return 0;
}

static int tpu_int(struct sg_card *card, struct v_channel *channel)
{
	struct cacheline_align_circ_buf *rx_buf = NULL;
	struct list_head *port_list;
	struct v_port *port = NULL;
	struct circ_buf *port_rx_buf = NULL;
	struct task_response_from_tpu response_from_tpu;
	int len = sizeof(response_from_tpu);
	int all_len = 0;
	int c;
	uint64_t head;
	uint64_t tail;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);

	rx_buf = channel->rx_buf;
	port_list = channel->port_list.next;
	port = container_of(port_list, struct v_port, list);
	port_rx_buf = &port->port_rx_buf;

	while (1) {
		rx_buf->circ_buf_read(&rx_buf->head, &head, sizeof(head));
		rx_buf->circ_buf_read(&rx_buf->tail, &tail, sizeof(tail));
		DBG_MSG("[ch:0x%llx] head:0x%llx, tail:0x%llx\n", channel->channel_index, head, tail);
		c = CIRC_CNT(head, tail, card->pool_size);
		if (c == 0)
			break;

		copy_from_circbuf_not_change_index((char *)&response_from_tpu, rx_buf,
						   sizeof(response_from_tpu), card->pool_size);
		response_from_tpu.kr_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		copy_to_circbuf(port_rx_buf, (char *)&response_from_tpu, sizeof(response_from_tpu), card->pool_size);

		all_len += len;
		tail = (tail + len) & (card->pool_size - 1);
		rx_buf->circ_buf_write(&rx_buf->tail, &tail, sizeof(tail));

	}
	DBG_MSG("[port:%s] get response, stream id:0x%llx, task id:0x%llx\n", port->name, response_from_tpu.stream_id,
		 response_from_tpu.task_id);
	channel->channel_info.int_rcv_cnt += all_len;
	atomic_add(all_len, &port->cnt_available);
	wake_up_all(&port->read_available);
	DBG_MSG("wake up tpu channel\n");

	return 0;
}

static int media_int(struct sg_card *card, struct v_channel *channel)
{
	struct cacheline_align_circ_buf *rx_buf = NULL;
	struct list_head *port_list;
	struct v_port *port = NULL;
	struct circ_buf *port_rx_buf = NULL;
	struct task_response_from_media response_from_media;
	int len = sizeof(struct task_response_from_media);
	int all_len = 0;
	int c;
	int from_len;
	int to_len;
	int length;

	rx_buf = channel->rx_buf;
	port_list = channel->port_list.next;
	port = container_of(port_list, struct v_port, list);
	port_rx_buf = &port->port_rx_buf;

	while (1) {
		DBG_MSG("[ch:0x%llx] head:0x%llx, tail:0x%llx\n", channel->channel_index,
			rx_buf->head, rx_buf->tail);
		c = CIRC_CNT(rx_buf->head, rx_buf->tail, card->pool_size);
		if (c == 0)
			break;


		copy_from_circbuf_not_change_index((char *)&response_from_media, rx_buf,
						   sizeof(response_from_media), card->pool_size);
		response_from_media.kr_time = read_arch_timer();
		copy_to_circbuf(port_rx_buf, (char *)&response_from_media, sizeof(response_from_media),
				card->pool_size);

		all_len += len;
		rx_buf->tail = (rx_buf->tail + len) & (card->pool_size - 1);

		if (response_from_media.response_size) {
			length = response_from_media.response_size;
			while (1) {
				from_len = CIRC_CNT_TO_END(rx_buf->head, rx_buf->tail, card->pool_size);
				to_len = CIRC_SPACE_TO_END(port_rx_buf->head, port_rx_buf->tail, card->pool_size);
				c = min(from_len, to_len);
				if (length < c)
					c = length;
				memcpy_fromio(port_rx_buf->buf + port_rx_buf->head, rx_buf->buf + rx_buf->tail, c);
				port_rx_buf->head = (port_rx_buf->head + c) & (card->pool_size - 1);
				rx_buf->tail = (rx_buf->tail + c) & (card->pool_size - 1);
				length -= c;
				if (length == 0)
					break;
			}

			all_len += response_from_media.response_size;
		}
	}
	DBG_MSG("[port:%s] get response, stream id:0x%llx, task id:0x%llx\n", port->name, response_from_media.stream_id,
		 response_from_media.task_id);
	channel->channel_info.int_rcv_cnt += all_len;
	atomic_add(all_len, &port->cnt_available);
	wake_up_all(&port->read_available);
	DBG_MSG("wake up media channel:%llu\n", channel->channel_index);

	return 0;
}

static irqreturn_t sgcard_interrupt(int irq, void *dev_id)
{
	struct sg_card *card = (struct sg_card *)dev_id;
	struct v_channel *channel = NULL;
	int i;
	int ch_index;

	ch_index = irq - card->host_channel_irq_base;
	channel = &card->channel[ch_index];

	channel_clr_irq(channel);

	if (channel->channel_irq_handel)
		channel->channel_irq_handel(card, channel);
	else
		pr_err("irq:%d for channel%d have no irq handler\n", irq, i);


	return IRQ_HANDLED;
}

static int sgcard_get_dtb_info(struct platform_device *pdev, struct sg_card *card)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev_of_node(dev);
	struct resource *regs;
	struct vector_info *vector;
	uint32_t cpu, cpus;
	int i;
	int j;
	int ret;

	cpus = num_online_cpus();

	ret = of_property_read_u32(dev_node, "share-memory-type", &card->share_memory_type);
	ret = of_property_read_u32(dev_node, "host-channel-num", &card->host_channel_count);
	ret = of_property_read_u32(dev_node, "tpu-channel-num", &card->tpu_channel_count);
	ret = of_property_read_u32(dev_node, "media-channel-num", &card->media_channel_count);
	ret = of_property_read_u32(dev_node, "channel-size", &card->pool_size);
	card->channel_count = card->host_channel_count + card->tpu_channel_count + card->media_channel_count;
	DBG_MSG("port-num = %u, poll-size=%u\n", card->channel_count, card->pool_size);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "share-memory");
	if (!regs)
		return pr_err("no registers defined\n");

	card->addr_start = regs->start;
	pr_err("share memory start addr:0x%llx\n", card->addr_start);
	if (card->share_memory_type) {
		card->membase = memremap(regs->start, resource_size(regs), MEMREMAP_WB);
		if (!card->membase) {
			pr_err("ioremap failed\n");
			goto failed;
		}
	} else {
		card->membase = devm_ioremap(dev, regs->start, resource_size(regs));
		if (!card->membase) {
			pr_err("ioremap failed\n");
			goto failed;
		}
	}

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "top-reg");
	if (regs) {
		card->top_base = devm_ioremap(dev, regs->start, resource_size(regs));
		if (!card->top_base) {
			pr_err("top base ioremap failed\n");
			goto failed;
		}
	}

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config_file");
	if (regs) {
		card->config_file = devm_ioremap(dev, regs->start, resource_size(regs));
		if (!card->config_file) {
			pr_err("config file ioremap failed\n");
			goto failed;
		}
	} else {
		pr_info("no config file\n");
	}

	DBG_MSG("membase: 0x%llx, top base: 0x%llx\n", (uint64_t)card->membase, (uint64_t)card->top_base);


	card->tx_pool_index = kmalloc_array(card->channel_count, sizeof(uint32_t), GFP_KERNEL);
	memset(card->tx_pool_index, 0, card->channel_count * sizeof(uint32_t));
	card->rx_pool_index = kmalloc_array(card->channel_count, sizeof(uint32_t), GFP_KERNEL);
	memset(card->rx_pool_index, 0, card->channel_count * sizeof(uint32_t));
	card->channel = kmalloc_array(card->channel_count, sizeof(struct v_channel), GFP_KERNEL);
	memset(card->channel, 0, card->channel_count * sizeof(struct v_channel));

	ret = of_property_read_u32_array(dev_node, "tx-channel", card->tx_pool_index, card->channel_count);
	if (ret) {
		pr_err("get tx pool index failed\n");
		goto iounmap;
	} else {
		for (i = 0; i < card->channel_count; i++)
			DBG_MSG("tx%d pool index:%u\n", i, card->tx_pool_index[i]);
	}
	ret = of_property_read_u32_array(dev_node, "rx-channel", card->rx_pool_index, card->channel_count);
	if (ret) {
		pr_err("get rx pool index failed\n");
		goto iounmap;
	} else {
		for (i = 0; i < card->channel_count; i++)
			DBG_MSG("rx%d pool index:%u\n", i, card->rx_pool_index[i]);
	}

	for (i = 0; i < card->channel_count; i++) {
		card->channel[i].irq = platform_get_irq(pdev, i);
		if (card->channel[i].irq < 0) {
			pr_err("vtty%d get irq num failed\n", i);
			goto free_request;
		} else {
			//cpu = i % cpus;
			cpu = 0;
			irq_set_affinity(card->channel[i].irq, get_cpu_mask(cpu));
			pr_err("ch%d irq:%llu->cpu%u\n", i, card->channel[i].irq, cpu);
		}

		ret = of_property_read_u64_index(dev_node, "virtual-msi", 2 * i,
						 &card->channel[i].tx_send_irq.phy_addr);
		if (ret) {
			pr_err("vtty%d get msi addr failed\n", i);
			goto free_request;
		} else {
			pr_err("msi pa:0x%llx\n", card->channel[i].tx_send_irq.phy_addr);
		}
		ret = of_property_read_u64_index(dev_node, "virtual-msi", 2 * i + 1,
						 &card->channel[i].tx_send_irq.msi_data);
		if (ret) {
			pr_err("vtty%d get msi data failed\n", i);
			goto free_request;
		} else {
			pr_err("msi data:0x%llx\n", card->channel[i].tx_send_irq.msi_data);
		}

		DBG_MSG("channel:%d, tx_phy_addr:%llx\n", i, card->channel[i].tx_send_irq.phy_addr);

		if (i == CHANNEL_HOST && card->channel[CHANNEL_HOST].tx_send_irq.phy_addr == 0) {
			DBG_MSG("enter pcie alloc vector\n");
			vector = sophgo_ep_alloc_vector(0, 0);
			if (vector != NULL) {
				pr_info("vector:0x%px, va:%px\n", vector, vector->msi_va);
				card->channel[CHANNEL_HOST].tx_send_irq = *vector;
			} else
				pr_err("[sgcard] failed to alloc pcie vector\n");
		}

		ret = of_property_read_u64_index(dev_node, "clr-irq", 2 * i, &card->channel[i].rx_clean_irq.phy_addr);
		if (ret) {
			pr_err("vtty%d get clr irq addr failed\n", i);
			goto free_request;
		}
		ret = of_property_read_u64_index(dev_node, "clr-irq", 2 * i + 1,
						 &card->channel[i].rx_clean_irq.clr_irq_data);
		if (ret) {
			pr_err("vtty%d get clr irq data failed\n", i);
			goto free_request;
		}

		if (card->channel[i].tx_send_irq.msi_va == 0) {
			card->channel[i].tx_send_irq.msi_va = devm_ioremap(dev, card->channel[i].tx_send_irq.phy_addr,
									PAGE_SIZE);
			if (!card->channel[i].tx_send_irq.msi_va) {
				pr_err("vtty%d remap msi addr failed\n", i);
				goto free_request;
			}
		}

		card->channel[i].rx_clean_irq.clr_irq_va = devm_ioremap(dev, card->channel[i].rx_clean_irq.phy_addr,
									PAGE_SIZE);
		if (!card->channel[i].rx_clean_irq.clr_irq_va) {
			pr_err("vtty%d remap clr addr failed\n", i);
			goto free_request;
		}

		sprintf(card->channel[i].name, "sg-channel%d", i);
		ret = request_irq(card->channel[i].irq, sgcard_interrupt, IRQF_TRIGGER_HIGH | IRQF_SHARED,
				  card->channel[i].name, card);
		if (ret < 0) {
			pr_err("index:%d request irq:0x%llx failed\n", i, card->channel[i].irq);
			goto free_request;
		}
	}

	card->host_channel_irq_base = card->channel[0].irq;
	card->tpu_channel_irq_base = card->channel[1].irq;
	if (card->media_channel_count)
		card->media_channel_irq_base = card->channel[card->tpu_channel_count + 1].irq;

	card->verify.verify_addr = vmalloc(MAX_HOST_REQUEST_SIZE);
	if (!card->verify.verify_addr) {
		pr_err("verify addr alloc failed\n");
		goto free_request;
	}

	return 0;

free_request:
	for (j = 0; j < card->channel_count; j++) {
		if (card->channel[j].tx_send_irq.msi_va)
			devm_iounmap(dev, card->channel[j].tx_send_irq.msi_va);
		if (card->channel[j].rx_clean_irq.clr_irq_va)
			devm_iounmap(dev, card->channel[j].rx_clean_irq.clr_irq_va);
		if (card->channel[j].irq)
			free_irq(card->channel[j].irq, card);
	}
iounmap:
	devm_iounmap(dev, card->membase);
failed:
	return -1;
}

static inline void sync_is(void)
{
	asm volatile (".long 0x01b0000b");
}

static int cache_memory_read(void *circ_buf, void *user_buf, uint64_t size)
{
	arch_invalidate_pmem(circ_buf, size);
	sync_is();
	memcpy(user_buf, circ_buf, size);

	return size;
}

static int cache_memory_write(void *circ_buf, void *user_buf, uint64_t size)
{
	memcpy(circ_buf, user_buf, size);
	arch_wb_cache_pmem(circ_buf, size);
	sync_is();

	return size;
}

static int device_memory_read(void *circ_buf, void *user_buf, uint64_t size)
{
	memcpy_fromio(user_buf, circ_buf, size);

	return size;
}

static int device_memory_write(void *circ_buf, void *user_buf, uint64_t size)
{
	memcpy_toio(circ_buf, user_buf, size);

	return size;
}

static int config_channel(struct sg_card *card)
{
	struct v_channel *channel;
	uint64_t phy_addr;
	uint64_t head = 0;
	uint64_t tail = 0;
	int i;

	for (i = 0; i < card->channel_count; i++) {
		channel = &card->channel[i];
		channel->channel_index = i;
		channel->tx_buf = card->membase + sizeof(struct cacheline_align_circ_buf) * card->tx_pool_index[i];
		channel->rx_buf = card->membase + sizeof(struct cacheline_align_circ_buf) * card->rx_pool_index[i];
		pr_err("%d: tx addr:%px, rx_addr:%px\n", i, channel->tx_buf, channel->rx_buf);
		if (card->share_memory_type) {
			channel->tx_buf->circ_buf_read = cache_memory_read;
			channel->tx_buf->circ_buf_write = cache_memory_write;
			channel->rx_buf->circ_buf_read = cache_memory_read;
			channel->rx_buf->circ_buf_write = cache_memory_write;
			pr_err("cacheable memory\n");
		} else {
			channel->tx_buf->circ_buf_read = device_memory_read;
			channel->tx_buf->circ_buf_write = device_memory_write;
			channel->rx_buf->circ_buf_read = device_memory_read;
			channel->rx_buf->circ_buf_write = device_memory_write;
			pr_err("device memory\n");
		}

		channel->tx_buf->circ_buf_write(&channel->tx_buf->head, &head, sizeof(head));
		channel->tx_buf->circ_buf_write(&channel->tx_buf->tail, &tail, sizeof(tail));
		channel->rx_buf->circ_buf_write(&channel->rx_buf->head, &head, sizeof(head));
		channel->rx_buf->circ_buf_write(&channel->rx_buf->tail, &tail, sizeof(tail));

		phy_addr = card->addr_start + MSGFIFO_HEAD_SIZE + card->pool_size * card->tx_pool_index[i];
		channel->tx_buf->circ_buf_write(&channel->tx_buf->phy_addr, &phy_addr, sizeof(phy_addr));
		pr_err("%d: tx buf addr:%px, paddr:0x%llx, read:0x%llx\n", i, &channel->tx_buf->phy_addr, phy_addr, channel->tx_buf->phy_addr);
		phy_addr = card->addr_start + MSGFIFO_HEAD_SIZE + card->pool_size * card->rx_pool_index[i];
		channel->rx_buf->circ_buf_write(&channel->rx_buf->phy_addr, &phy_addr, sizeof(phy_addr));
		pr_err("%d: rx buf addr:%px, paddr:0x%llx\n", i, &channel->rx_buf->phy_addr, phy_addr);

		channel->tx_buf->buf = card->membase + MSGFIFO_HEAD_SIZE
						   + card->pool_size * card->tx_pool_index[i];
		channel->rx_buf->buf = card->membase + MSGFIFO_HEAD_SIZE
						   + card->pool_size * card->rx_pool_index[i];

		mutex_init(&channel->tx_lock);
		spin_lock_init(&channel->port_lock);
		INIT_LIST_HEAD(&channel->port_list);
		INIT_LIST_HEAD(&channel->channel_info.rp_list);

		if (i == 0) {
			INIT_DELAYED_WORK(&channel->channel_delayed_work, host_int_work_func);
			channel->channel_irq_handel = host_int;
		} else if (i >= CHANNEL_TPU0 && i < CHANNEL_TPU0 + card->tpu_channel_count) {
			channel->channel_irq_handel = tpu_int;
		}else
			channel->channel_irq_handel = media_int;
	}

	return 0;
}

static ssize_t sg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct v_port *port = file->private_data;
	struct sg_card *card = port->card;
	struct circ_buf *rx_buf = &port->port_rx_buf;
	size_t length;
	size_t real_count;
	int ret;
	int c;
	struct host_request_action action;
	struct task_response_from_tpu response;
	int i;
	int read_request = 0;
	int read_response = 0;
	char *tmp_buf;

	real_count = count >> READ_TYPE_SHIFT;
	c = CIRC_CNT(rx_buf->head, rx_buf->tail, card->pool_size);
	if (c < real_count)
		return 0;

	if ((count & READ_TYPE) == READ_REQUEST_HEAD) {
		read_request = 1;
		tmp_buf = (char *)&action;
	} else if ((count & READ_TYPE) == READ_TPU_RESPONSE) {
		read_response = 1;
		tmp_buf = (char *)&response;
	}

	length = real_count;

	DBG_MSG("[port:%s] read 0x%lx bytes, head:0x%x, tail:0x%x\n", port->name, real_count,
		 rx_buf->head, rx_buf->tail);
	while (1) {
		c = CIRC_CNT_TO_END(rx_buf->head, rx_buf->tail, card->pool_size);
		if (length < c)
			c = length;
		smp_mb();
		DBG_MSG("to user %d bytes\n", c);
		ret = copy_to_user(buf, rx_buf->buf + rx_buf->tail, c);
		if (ret) {
			pr_err("%s called by %s failed\n", __func__, current->comm);
			break;
		}

		if (read_request | read_response) {
			DBG_MSG("to tmp buf %d bytes\n", c);
			memcpy(tmp_buf, rx_buf->buf + rx_buf->tail, c);
		}
		rx_buf->tail = (rx_buf->tail + c) & (card->pool_size - 1);
		buf += c;
		tmp_buf += c;
		length -= c;
		if (length == 0) {
			ret = 0;
			break;
		}
	}
	DBG_MSG("[port:%s] read over, head:0x%x, tail:0x%x\n", port->name, rx_buf->head, rx_buf->tail);
	atomic_sub(real_count, &port->cnt_available);
	port->port_info.rcv_bytes += real_count;

	if (read_request) {
		if (action.type == ERROR_REQUEST_RESPONSE || action.type > SETUP_C2C_REQUEST) {
			for (i = 0; i < sizeof(action) / sizeof(uint64_t); i++)
				pr_err("offset:%d data:0x%llx\n", i, ((uint64_t *)(&action))[i]);
		}

		DBG_MSG("[port:%s] [%s]-0x%llx, task size:0x%x\n", port->name, request_response_type[action.type],
			 action.request_id, action.task_size);
	} else if (read_response) {
		DBG_MSG("[port:%s] response task id:0x%llx, result:0x%llx\n", port->name, response.task_id,
			 response.result);
	}

	if (ret)
		return -EOPNOTSUPP;

	return real_count;
}

static ssize_t sg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct v_port *port = file->private_data;
	struct v_channel *channel = port->parent_channel;
	struct sg_card *card = port->card;
	struct cacheline_align_circ_buf *tx_buf = channel->tx_buf;
	size_t length = count;
	size_t send_length = 0;
	struct record_response_action *response;
	int ret;
	int c;
	uint64_t head;
	uint64_t tail;

	struct host_response_action *action = (struct host_response_action *)port->write_buf;
	struct task_head *task_head = (struct task_head *)port->write_buf;
	int i;

	ret = copy_from_user(port->write_buf, buf, count);
	if (ret) {
		pr_err("%s failed\n", __func__);
		return 0;
	}

	mutex_lock(&channel->tx_lock);
	head = tx_buf->head;
	DBG_MSG("[port:%s], ch-%lld-tx head:0x%llx, tail:0x%llx\n", port->name, channel->channel_index, tx_buf->head,
		tx_buf->tail);

	while (1) {
		tx_buf->circ_buf_read(&tx_buf->tail, &tail, sizeof(tail));
		c = CIRC_SPACE_TO_END(head, tail, card->pool_size);
		if (length < c)
			c = length;

		tx_buf->circ_buf_write(tx_buf->buf + head + i, port->write_buf + send_length + i, c);

		head = (head + c) & (card->pool_size - 1);
		length -= c;
		send_length += c;
		if (length == 0) {
			ret = 0;
			break;
		}
	}
	tx_buf->circ_buf_write(&tx_buf->head, &head, sizeof(head));
	DBG_MSG("[port:%s], ch-%lld-tx update head:0x%llx, tail:0x%llx\n", port->name, channel->channel_index,
		 head, tx_buf->tail);
	if (channel->channel_index != CHANNEL_HOST) {
		DBG_MSG("[port:%s] [%s]-0x%llx, tsk body size:0x%llx\n", port->name, task_type[task_head->task_type],
			 task_head->task_id, task_head->task_body_size);
	} else if (channel->channel_index == CHANNEL_HOST) {
		port->port_info.last_tx_msg_type = action->type;
		port->port_info.all_msg_cnt[action->type]++;

		if (record_response) {
			response = kzalloc(sizeof(struct record_response_action), GFP_KERNEL);
			if (response == NULL)
				goto send_irq;

			response->response_body = *action;
			list_add_tail(&response->list, &channel->channel_info.rp_list);
		}

		DBG_MSG("[port:%s] [%s]-0x%llx\n", port->name, request_response_type[action->type],
			 action->response_id);
	} else {

	}

send_irq:
	channel_tx_send_irq(channel);
	port->port_info.send_bytes += count;
	mutex_unlock(&channel->tx_lock);

	if (ret)
		return -EFAULT;

	return count;
}

static int sg_open(struct inode *inode, struct file *file)
{
	struct v_port *port = container_of(inode->i_cdev, struct v_port, cdev);

	DBG_MSG("[stream:0x%llx] sg open\n", port->stream_id);
	if (atomic_read(&port->cnt_opened)) {
		pr_err("file has been opened\n");

		// return -EBUSY;
	}

	file->private_data = port;
	atomic_set(&port->cnt_opened, 1);

	return 0;
}

#if 0
static void destroy_stream_work_func(struct work_struct *work)
{
	struct v_port *port = container_of(work, struct v_port, destroy_stream_work.work);

	destroy_stream_cdev_by_fd(port);
}
#endif

static int sg_close(struct inode *inode, struct file *file)
{
	struct v_port *port = file->private_data;

	if (atomic_read(&port->cnt_opened) != 1) {
		pr_err("close file failed\n");

		return -EMFILE;
	}

	atomic_set(&port->cnt_opened, -1);
	destroy_stream_cdev_by_fd(port);
	// INIT_DELAYED_WORK(&port->destroy_stream_work, destroy_stream_work_func);
	// schedule_delayed_work(&port->destroy_stream_work, 10);

	return 0;
}

static int sg_mmap(struct file *file, struct vm_area_struct *vma)
{
	DBG_MSG("%s called by %s\n", __func__, current->comm);
	return -EOPNOTSUPP;
}

void sophgo_setup_c2c(void);

static long sg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct v_port *port = file->private_data;
	struct sg_card *card = port->card;
	struct sg_stream_info stream_info;
	int c2c_loop;
	int c2c_ok = 0;

	DBG_MSG("[stream:0x%llx] cmd: 0x%x\n", port->stream_id, cmd);
	switch (cmd) {
	case SG_IOC_STREAM_CREATE:
		if (copy_from_user(&stream_info, (void __user *)arg, sizeof(stream_info)))
			return -EFAULT;

		create_stream_cdev(card, &stream_info);
		stream_info.ret = 0;
		if (copy_to_user((void __user *)arg, &stream_info, sizeof(stream_info)))
			return -EFAULT;
		break;
	case SG_IOC_SETUP_C2C:
		sophgo_setup_c2c();
		for (c2c_loop = 0; c2c_loop < 20; c2c_loop++) {
			c2c_ok = sophgo_check_c2c();
			if (c2c_ok < 0)
				break;
			msleep(1000);
		}

		if (c2c_ok >= 0) {
			if (copy_to_user((void __user *)arg, &c2c_ok, sizeof(c2c_ok)))
				pr_err("failed copy to userspace\n");

			return -EFAULT;
		} else
			pr_err("all c2c link up success\n");

		break;
	default:
		pr_err("unknown ioctl command 0x%x\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static __poll_t sg_poll(struct file *file, poll_table *wait)
{
	struct v_port *port = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &port->read_available, wait);
	if (atomic_read(&port->cnt_available))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static const struct file_operations sg_fops = {
	.read = sg_read,
	.write = sg_write,
	.open = sg_open,
	.release = sg_close,
	.unlocked_ioctl = sg_ioctl,
	.poll = sg_poll,
	.mmap = sg_mmap,
	.owner = THIS_MODULE,
};

static int create_stream_cdev(struct sg_card *card, struct sg_stream_info *stream_info)
{
	struct v_channel *channel = &card->channel[stream_info->channel_index];
	struct v_port *port = NULL;
	int ret;
	unsigned long flags;

	port = kzalloc(sizeof(struct v_port), GFP_KERNEL);
	if (IS_ERR(port)) {
		pr_err("sg create cdev failed\n");
		return -ENOENT;
	}

	sprintf(port->name, "sg-stream-file-%lld", stream_info->stream_id);
	DBG_MSG("CREATE %s\n", port->name);
	ret = find_and_set_devno_map(card);
	if (ret < 0) {
		pr_err("failed to find devno\n");
		return -ENOENT;
	}
	atomic_set(&port->cnt_available, 0);
	atomic_set(&port->wake_up_task_cnt, 0);
	init_waitqueue_head(&port->read_available);
	port->devno = card->cdev_info.devno + ret;
	port->parent = card->cdev_info.dev;
	port->parent_channel = channel;
	port->card = card;
	port->dev = device_create(card->cdev_info.sg_class, port->parent, port->devno, NULL, port->name);
	if (IS_ERR(port->dev)) {
		DBG_MSG("CREATE %s failed\n", port->name);
		return -ENOENT;
	}

	cdev_init(&port->cdev, &sg_fops);
	port->cdev.owner = THIS_MODULE;
	port->stream_id = stream_info->stream_id;
	port->port_rx_buf.buf = kzalloc(card->pool_size, GFP_KERNEL);
	if (!port->port_rx_buf.buf) {
		pr_err("stream port kzalloc rx buf failed\n");
		return -ENOENT;
	}
	DBG_MSG("[stream:0x%llx] create port, port->port_rx_buf.buf=0x%px pa=0x%lx\n",
		port->stream_id, port->port_rx_buf.buf, virt_to_phys(port->port_rx_buf.buf));
	port->write_buf = kzalloc(card->pool_size, GFP_KERNEL);
	if (!port->write_buf) {
		pr_err("stream port kzalloc write buf failed\n");
		return -ENOENT;
	}
	ret = cdev_add(&port->cdev, port->devno, 1);
	if (ret) {
		DBG_MSG("cdev add failed\n");
		return -ENOENT;
	}
	spin_lock_irqsave(&channel->port_lock, flags);
	list_add_tail(&port->list, &channel->port_list);
	channel->channel_info.port_cnt++;
	spin_unlock_irqrestore(&channel->port_lock, flags);

	return 0;
}

static int destroy_stream_cdev_by_fd(struct v_port *port)
{
	struct v_channel *channel = port->parent_channel;
	struct sg_card *card = port->card;
	unsigned long flags;

	spin_lock_irqsave(&channel->port_lock, flags);
	list_del(&port->list);
	spin_unlock_irqrestore(&channel->port_lock, flags);

	cdev_del(&port->cdev);
	device_destroy(card->cdev_info.sg_class, port->devno);
	clr_card_devno_map(card, (port->devno - card->cdev_info.devno));
	DBG_MSG("clr_card_devno_map %d\n", (port->devno - card->cdev_info.devno));
	channel->channel_info.port_cnt--;

	kfree(port->port_rx_buf.buf);
	kfree(port->write_buf);
	kfree(port);

	return 0;
}

__attribute__((unused)) static int destroy_stream_cdev(struct sg_card *card, struct sg_stream_info *stream_info)
{
	struct v_channel *channel = &card->channel[stream_info->channel_index];
	struct v_port *port = NULL;
	unsigned long flags;

	spin_lock_irqsave(&channel->port_lock, flags);
	list_for_each_entry(port, &channel->port_list, list) {
		if (port->stream_id == stream_info->stream_id)
			break;
	}
	//TODO: there are no cdev which stream id is match
	list_del(&port->list);
	spin_unlock_irqrestore(&channel->port_lock, flags);

	cdev_del(&port->cdev);
	device_destroy(card->cdev_info.sg_class, port->devno);

	kfree(port->port_rx_buf.buf);
	kfree(port);

	return 0;
}


static int sg_wake_stream_open(struct inode *inode, struct file *file)
{
	struct wake_up_stream_port *port = container_of(inode->i_cdev, struct wake_up_stream_port, cdev);
	int cnt_opened;

	DBG_MSG("%s open wake_up_stream\n", current->comm);

	file->private_data = port;
	cnt_opened = atomic_add_return(1, &port->cnt_opened);

	DBG_MSG("current cnt_opened:%d\n", cnt_opened);

	return 0;
}

static int sg_wake_stream_close(struct inode *inode, struct file *file)
{
	struct wake_up_stream_port *port = file->private_data;
	int cnt_opened;

	DBG_MSG("%s open wake_up_stream\n", current->comm);
	cnt_opened = atomic_sub_return(1, &port->cnt_opened);

	DBG_MSG("current cnt_opened:%d\n", cnt_opened);

	return 0;
}

static long sg_wake_stream_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wake_up_stream_port *wake_up_port = file->private_data;
	struct sg_card *card = wake_up_port->card;
	struct v_channel *channel = &card->channel[CHANNEL_HOST];
	struct v_port *port;
	uint64_t stream_id;
	unsigned long flags;


	if (copy_from_user(&stream_id, (void __user *)arg, sizeof(uint64_t)))
		return -EFAULT;

	DBG_MSG("cmd: 0x%x, stream id:0x%llx\n", cmd, stream_id);

	switch (cmd) {
	case SG_WAKE_UP_STREAM:
		if (stream_id == WAKE_UP_ALL_STREAM) {
			spin_lock_irqsave(&channel->port_lock, flags);
			list_for_each_entry(port, &channel->port_list, list) {
				if (port->stream_id == 0)
					continue;
				atomic_add(1, &port->wake_up_task_cnt);
				DBG_MSG("[%s] wake up cnt:0x%x\n", port->name, port->wake_up_task_cnt.counter);
			}
			spin_unlock_irqrestore(&channel->port_lock, flags);

			wake_up(&wake_up_port->resource_available);
			DBG_MSG("wake_up_all_stream\n");
		} else {
			DBG_MSG("wake up stream:0x%llx\n", stream_id);
			//TODO: for each stream
		}
		break;
	case SG_STREAM_RUNNING:
		spin_lock_irqsave(&channel->port_lock, flags);
		list_for_each_entry(port, &channel->port_list, list) {
			if (stream_id == port->stream_id) {
				if (atomic_read(&port->wake_up_task_cnt)) {
					DBG_MSG("wake up task cnt:0x%x\n", atomic_read(&port->wake_up_task_cnt));
					atomic_dec(&port->wake_up_task_cnt);
				}

				break;
			}
		}
		spin_unlock_irqrestore(&channel->port_lock, flags);
		break;
	default:
		pr_err("unknown ioctl command 0x%x\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static __poll_t sg_wake_stream_poll(struct file *file, poll_table *wait)
{
	struct wake_up_stream_port *wake_up_port = file->private_data;
	struct v_channel *channel = &wake_up_port->card->channel[CHANNEL_HOST];
	struct v_port *port;
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(file, &wake_up_port->resource_available, wait);

	spin_lock_irqsave(&channel->port_lock, flags);
	list_for_each_entry(port, &channel->port_list, list) {
		if (atomic_read(&port->wake_up_task_cnt)) {
			mask |= EPOLLIN | EPOLLRDNORM;
			break;
		}
	}
	spin_unlock_irqrestore(&channel->port_lock, flags);

	return mask;
}

static const struct file_operations sg_wake_stream_fops = {
	.open = sg_wake_stream_open,
	.release = sg_wake_stream_close,
	.unlocked_ioctl = sg_wake_stream_ioctl,
	.poll = sg_wake_stream_poll,
	.owner = THIS_MODULE,
};

static int sg_create_wake_stream_cdev(struct device *dev, struct sg_card *card)
{
	struct wake_up_stream_port *port = &card->wake_up_stream_port;
	int ret;

	sprintf(port->name, "wake-up-sg-stream");
	pr_err("CREATE %s\n", port->name);
	atomic_set(&port->cnt_opened, 0);
	init_waitqueue_head(&port->resource_available);
	set_card_devno_map(card, card->channel_count);
	port->devno = card->cdev_info.devno + card->channel_count;
	port->parent = dev;
	port->card = card;
	port->dev = device_create(card->cdev_info.sg_class, port->parent, port->devno, NULL, port->name);
	if (IS_ERR(port->dev)) {
		pr_err("CREATE %s failed\n", port->name);
		return -ENOENT;
	}
	cdev_init(&port->cdev, &sg_wake_stream_fops);
	port->cdev.owner = THIS_MODULE;
	ret = cdev_add(&port->cdev, port->devno, 1);
	if (ret) {
		pr_err("wake up stream cdev add failed\n");
		return -ENOENT;
	}

	return 0;
}

static int sg_create_cdev(struct device *dev, struct sg_card *card)
{
	struct v_channel *channel = NULL;
	struct v_port *port = NULL;
	int ret;
	int i;
	unsigned long flags;

	if (card->cdev_info.sg_class == NULL) {
		card->cdev_info.sg_class = compat_class_create(THIS_MODULE, DRV_NAME);
		if (IS_ERR(card->cdev_info.sg_class)) {
			pr_err("create class error\n");
			card->cdev_info.sg_class = NULL;
			return -ENOENT;
		}
	}

	ret = alloc_chrdev_region(&card->cdev_info.devno, 0, PORT_MAX, "sgcard channel");
	if (ret < 0) {
		pr_err("register char device error\n");
		return -ENOENT;
	}

	for (i = 0; i < card->channel_count; i++) {
		channel = &card->channel[i];
		port = kzalloc(sizeof(struct v_port), GFP_KERNEL);
		if (IS_ERR(port)) {
			pr_err("sg create cdev failed\n");
			return -ENOENT;
		}

		sprintf(port->name, "sgcard-channel-%d", i);
		DBG_MSG("CREATE %s\n", port->name);
		atomic_set(&port->cnt_available, 0);
		atomic_set(&port->wake_up_task_cnt, 0);
		init_waitqueue_head(&port->read_available);
		set_card_devno_map(card, i);
		port->devno = card->cdev_info.devno + i;
		port->parent = dev;
		port->parent_channel = channel;
		port->card = card;
		port->dev = device_create(card->cdev_info.sg_class, port->parent, port->devno, NULL, port->name);
		cdev_init(&port->cdev, &sg_fops);
		port->cdev.owner = THIS_MODULE;
		port->port_rx_buf.buf = kzalloc(card->pool_size, GFP_KERNEL);
		if (IS_ERR(port->port_rx_buf.buf)) {
			pr_err("port kzalloc rx buf failed\n");
			return -ENOENT;
		} else {
			pr_err("port->port_rx_buf:%px\n", &port->port_rx_buf);
		}
		port->write_buf = kzalloc(card->pool_size, GFP_KERNEL);
		if (!port->write_buf) {
			pr_err("stream port kzalloc write buf failed\n");
			return -ENOENT;
		}

		cdev_add(&port->cdev, port->devno, 1);
		spin_lock_irqsave(&channel->port_lock, flags);
		list_add_tail(&port->list, &channel->port_list);
		channel->channel_info.port_cnt++;
		spin_unlock_irqrestore(&channel->port_lock, flags);
	}

	sg_create_wake_stream_cdev(dev, card);

	return 0;
}

static int sg_destroy_cdev(struct device *dev, struct sg_card *card)
{
	struct v_channel *channel = NULL;
	struct v_port *port = NULL;
	struct list_head *port_list;
	unsigned long flags;
	int i;

	for (i = 0; i < card->channel_count; i++) {
		channel = &card->channel[i];
		if (!list_empty(&channel->port_list)) {
			port_list = channel->port_list.next;
			port = container_of(port_list, struct v_port, list);
			cdev_del(&port->cdev);
			device_destroy(card->cdev_info.sg_class, port->devno);
			spin_lock_irqsave(&channel->port_lock, flags);
			list_del(&port->list);
			spin_unlock_irqrestore(&channel->port_lock, flags);
			kfree(port->port_rx_buf.buf);
			kfree(port);
		}
	}

	unregister_chrdev_region(card->cdev_info.devno, PORT_MAX);
	class_destroy(card->cdev_info.sg_class);

	return 0;
}

static int msg_append(char *base, unsigned long limit,
		   const char *fmt, ...)
{
	int len = strlen(base);
	va_list arg;

	va_start(arg, fmt);
	len += vsnprintf(base + len, limit - len, fmt, arg);
	va_end(arg);
	return len;
}

static ssize_t card_info_store(struct device *dev,
			       struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t card_info_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct sg_card *card = dev_get_drvdata(dev);
	struct v_channel *channel;
	struct v_port *port;
	int i;
	int j;

	msg_append(buf, PAGE_SIZE, "channel count:0x%llx\n", card->channel_count);

	for (i = 0; i < card->channel_count; i++) {
		channel = &card->channel[i];
		msg_append(buf, PAGE_SIZE, "CHANNEL%d:include %d ports, int all rcv 0x%llx bytes\n",
			   i, channel->channel_info.port_cnt, channel->channel_info.int_rcv_cnt);
		msg_append(buf, PAGE_SIZE, "|\n");
		list_for_each_entry(port, &channel->port_list, list) {
			msg_append(buf, PAGE_SIZE, "|--devno:0x%x,rcv bytes:0x%llx, send bytes:0x%llx\n", port->devno,
				   port->port_info.rcv_bytes, port->port_info.send_bytes);
			for (j = STREAM_CREATE_REQUEST; j <= EVENT_TRIGGERED; j++) {
				if (port->port_info.all_msg_cnt[j])
					msg_append(buf, PAGE_SIZE, "   |--[%s]:0x%llx\n", request_response_type[j],
						   port->port_info.all_msg_cnt[j]);
			}
			msg_append(buf, PAGE_SIZE, "   |--last tx msg type:%s\n   |--last rx msg type:%s\n",
				   request_response_type[port->port_info.last_tx_msg_type],
				   request_response_type[port->port_info.last_rx_msg_type]);
		}

	}

	return msg_append(buf, PAGE_SIZE, "addr start:0x%llx\n", card->addr_start);
}

static ssize_t debug_enable_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	char buf[32] = {0};
	int enable;
	int ret;

	pr_err("debug enable store\n");
	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &enable);

	if (enable == 0 || enable == 1) {
		pr_err("enable = %d\n", enable);
		debug_enable = enable;
		return len;
	} else {
		return -EINVAL;
	}
}

static ssize_t debug_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return msg_append(buf, PAGE_SIZE, "debug enable:%d\n", debug_enable);
}

static ssize_t recorded_response_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	struct sg_card *card = dev_get_drvdata(dev);
	struct v_channel *channel = &card->channel[CHANNEL_HOST];
	struct record_response_action *response;
	struct record_response_action *tmp;
	char buf[32] = {0};
	int enable;
	int ret;

	DBG_MSG("record response enable store\n");
	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &enable);

	if (enable == 0 || enable == 1) {
		pr_err("enable = %d\n", enable);
		record_response = enable;

		if (enable == 0) {
			mutex_lock(&channel->tx_lock);
			list_for_each_entry_safe(response, tmp, &channel->channel_info.rp_list, list) {
				list_del(&response->list);
				kfree(response);
			}

			mutex_unlock(&channel->tx_lock);
		}

		return len;
	} else {
		return -EINVAL;
	}
}

static ssize_t recorded_response_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sg_card *card = dev_get_drvdata(dev);
	struct v_channel *channel = &card->channel[CHANNEL_HOST];
	struct record_response_action *record;
	struct record_response_action *tmp;
	struct host_response_action *response;
	int ret;

	if (record_response == 0)
		return msg_append(buf, PAGE_SIZE, "recorded reponse is not enable\n");

	mutex_lock(&channel->tx_lock);

	list_for_each_entry_safe(record, tmp, &channel->channel_info.rp_list, list) {
		response = &record->response_body;
		msg_append(buf, PAGE_SIZE, "rp:0x%lx\n", response->response_id);
		msg_append(buf, PAGE_SIZE, "type:[%s]\n", request_response_type[response->type]);
		msg_append(buf, PAGE_SIZE, "stream id:0x%lx\n", response->stream_id);
		msg_append(buf, PAGE_SIZE, "task id:0x%lx\n", response->task_id);
		if (response->type == TASK_DONE_RESPONSE || response->type == BTM_TASK_DONE_RESPONSE ||
		    response->type == EVENT_TRIGGERED || response->type == CALLBACK_RELEASE) {
			msg_append(buf, PAGE_SIZE, "wait_res_t:%lu, s2w:%lu\n", response->time.wait_resource_time,
				   response->time.start_time - response->time.wait_resource_time);
		} else {
			msg_append(buf, PAGE_SIZE, "kr:%lu, ur:%lu, ur-kr:%lu, s-ur:%lu\n",
				   response->time.kr_time, response->time.ur_time,
				   response->time.ur_time - response->time.kr_time,
				   response->time.start_time - response->time.ur_time);
		}

		msg_append(buf, PAGE_SIZE, "start_t:%lu, end_t:%lu, exec_t:%lu (ns)\n", response->time.start_time,
			   response->time.end_time, response->time.end_time - response->time.start_time);
		if (response->type == TASK_DONE_RESPONSE || response->type == BTM_TASK_DONE_RESPONSE) {
			msg_append(buf, PAGE_SIZE, "tp_s:%lu,tp_e:%lu,exec_t:%lu\n", response->time.tp_start_time,
				   response->time.tp_end_time,
				   response->time.tp_end_time - response->time.tp_start_time);
			msg_append(buf, PAGE_SIZE, "ts-s:%lu, e-te:%lu\n",
				   response->time.tp_start_time - response->time.start_time,
				   response->time.end_time - response->time.tp_end_time);
			msg_append(buf, PAGE_SIZE, "kr-te:%lu\n", response->time.kr_time - response->time.tp_end_time);
		}
		ret = msg_append(buf, PAGE_SIZE, "\n");
		list_del(&record->list);
		kfree(record);
		if (ret > PAGE_SIZE - 200)
			break;
	}

	mutex_unlock(&channel->tx_lock);

	if (ret > PAGE_SIZE - 100) {
		msg_append(buf, PAGE_SIZE, "oops, over flow, please re-run ");
		return msg_append(buf, PAGE_SIZE, "cat */recorded_response to show remaining response\n");
	}

	return msg_append(buf, PAGE_SIZE, "all response have been displayed\n");
}

static ssize_t verify_enable_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	struct sg_card *card = dev_get_drvdata(dev);
	char buf[32] = {0};
	int enable;
	int ret;

	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &enable);

	if (enable == 0 || enable == 1) {
		card->verify.verify_enable = enable;
		pr_err("verify %s\n", enable ? "enable" : "disable");

		return len;
	} else {
		return -EINVAL;
	}
}

static ssize_t verify_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sg_card *card = dev_get_drvdata(dev);

	return msg_append(buf, PAGE_SIZE, "verify %s\n", card->verify.verify_enable ? "enable" : "disable");
}

static DEVICE_ATTR_RW(card_info);
static DEVICE_ATTR_RW(debug_enable);
static DEVICE_ATTR_RW(recorded_response);
static DEVICE_ATTR_RW(verify_enable);


static ssize_t tpurt_config_proc_read(struct file *fp, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sg_card *card = get_card();
	void *config_addr;
	int len;

	config_addr = kmalloc(0x1000, GFP_KERNEL);
	if (config_addr == NULL) {
		pr_err("failed to alloc config addr\n");
		return -1;
	}

	memcpy_fromio(config_addr, card->config_file, 0x1000);
	len = strlen(config_addr);
	if (*ppos >= len)
		return 0;

	if (count > len - *ppos)
		count = len - *ppos;

	if (copy_to_user(user_buf, config_addr, count)) {
		pr_err("failed copy config to user buf\n");
		return -EFAULT;
	}

	*ppos += count;

	return count;
}

static struct proc_ops tpurt_config_fops = {
	.proc_read = tpurt_config_proc_read
};

static int create_sg_proc_file(struct device *dev, struct sg_card *card)
{
	card->tpurt_dir = proc_mkdir("tpurt", NULL);
	if (!card->tpurt_dir) {
		pr_err("Unable to create /proc/tpurt directory\n");
		return -1;
	}

	card->tpurt_proc = proc_create("config", 0444, card->tpurt_dir, &tpurt_config_fops);
	if (!card->tpurt_proc) {
		pr_err("Unable to create /proc/tpurt/config file\n");
		proc_remove(card->tpurt_dir);

		return -1;
	}

	return 0;
}

static int sgcard_create_file(struct device *dev, struct sg_card *card)
{
	int ret;

	ret = device_create_file(dev, &dev_attr_card_info);
	ret = device_create_file(dev, &dev_attr_debug_enable);
	ret = device_create_file(dev, &dev_attr_recorded_response);
	ret = device_create_file(dev, &dev_attr_verify_enable);

	if (card->config_file)
		ret = create_sg_proc_file(dev, card);

	return ret;
}

static int sgcard_remove_file(struct device *dev)
{
	device_remove_file(dev, &dev_attr_card_info);
	device_remove_file(dev, &dev_attr_debug_enable);

	return 0;
}

static int sgcard_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sg_card *card;
	int ret = 0;

	DBG_MSG("[sgcard]:begin load drvier\n");

	card = kzalloc(sizeof(struct sg_card), GFP_KERNEL);
	if (!card)
		goto fail;
	set_card(card);
	dev_set_drvdata(dev, card);
	card->cdev_info.dev = dev;

	card->pcie_vfun = device_get_match_data(dev);

	if (sgcard_get_dtb_info(pdev, card)) {
		pr_err("get dtb info failed\n");
		goto free_card;
	}

	config_channel(card);
	sg_create_cdev(dev, card);
	sgcard_create_file(dev, card);

	/*notify the host that the driver has been executed*/
	if (card->top_base)
		writel(DRIVER_EXECED, card->top_base + RUUNING_STAGES);

	return ret;

free_card:
	kfree(card);

fail:
	pr_err("malloc sg card failed\n");
	return ret;
}

static void sgcard_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sg_card *card = dev_get_drvdata(dev);
	int j;

	sgcard_remove_file(dev);

	sg_destroy_cdev(dev, card);

	for (j = 0; j < card->channel_count; j++) {
		if (card->channel[j].tx_send_irq.msi_va)
			devm_iounmap(dev, card->channel[j].tx_send_irq.msi_va);
		if (card->channel[j].rx_clean_irq.clr_irq_va)
			devm_iounmap(dev, card->channel[j].rx_clean_irq.clr_irq_va);
		if (card->channel[j].irq)
			free_irq(card->channel[j].irq, card);
	}

	devm_iounmap(dev, card->membase);
	kfree(card);

	return;
}

static struct of_device_id sophgo_card_of_match[] = {
	{ .compatible = "sophgo,sophgo-card",},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_card_of_match);

static struct platform_driver sg_card_platform_driver = {
	.driver = {
		.name		= "sophgo-card",
		.of_match_table	= sophgo_card_of_match,
	},
	.probe			= sgcard_probe,
	.remove			= sgcard_remove,
};

static struct platform_driver *const drivers[] = {
	&sg_card_platform_driver,
};

int probe_sgcard(struct sophgo_pcie_vfun *sg_vfun)
{
	sophgo_card_of_match[0].data = sg_vfun;
	platform_register_drivers(drivers, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(probe_sgcard);

module_platform_driver(sg_card_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for sg runtime");

