/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SGDRV_INTERNAL_
#define _SGDRV_INTERNAL_

#define DRV_NAME "sgdrv"

#define DBG_MSG(fmt, args...) \
do { \
	if (debug_enable == 1) \
		pr_info("[sg]%s:" fmt, __func__, ##args); \
} while (0)

struct sg_dev {
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	dev_t devno;
	char dev_name[128];
	unsigned int id; // start from 0, use device name to align with user space
};

enum {
	STREAM_CREATE_REQUEST = 1,
	STREAM_CREATE_RESPONSE,

	STREAM_DESTROY_REQUEST,
	STREAM_DESTROY_RESPONSE,

	MALLOC_DEVICE_MEM_REQUEST,
	MALLOC_DEVICE_MEM_RESPONSE,

	FREE_DEVICE_MEM_REQUEST,
	FREE_DEVICE_MEM_RESPONSE,

	CALLBACK_REQUEST,
	CALLBACK_RESPONSE,
	CALLBACK_RELEASE,

	EVENT_CREATE_REQUEST,
	EVENT_CREATE_RESPONSE,

	TASK_CREATE_REQUEST,
	TASK_CREATE_RESPONSE,

	EVENT_TRIGGERED,
	TASK_DONE_RESPONSE,
	BTM_TASK_DONE_RESPONSE,
	TASK_ERROR_RESPONSE,
	FORCE_QUIT_REQUEST,
	SETUP_C2C_REQUEST,
	SETUP_C2C_RESPONSE,

	AP_MSG_COUNT,
};

int send_request(struct sg_dev *hdev, msg_t msg, void *msg_body, int msg_len, int sync);
int msg_handler(struct sg_dev *hdev, msg_t msg, void *msg_body, int msg_len);
void start_msgfifo(struct sg_dev *hdev);

#endif
