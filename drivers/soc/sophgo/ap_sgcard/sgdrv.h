/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SGDRV_
#define _SGDRV_

#define SG_DEV_PREFIX "sgdrv"
#define CHANNEL_UPSTREAM	0
struct sg_stream_info {
	uint64_t stream_id;
	uint64_t channel_index;
	int ret;
};

struct sg_callback_info {
	unsigned int id;
	unsigned int stream_id;
	int result;
};

struct sg_event_info {
	unsigned int id;
	unsigned int stream_id;
	unsigned int record_counter;
	int result;
};

struct sg_task_info {
	unsigned int id;
	unsigned int stream_id;
	int result;
};

#define SG_IOC_TEST			_IOWR('W', 0, int)
#define SG_IOC_STREAM_CREATE		_IOWR('W', 1, struct sg_stream_info)
#define SG_IOC_STREAM_DESTROY		_IOWR('W', 2, struct sg_stream_info)
#define SG_IOC_SETUP_C2C		_IOWR('W', 3, int)

#define SG_WAKE_UP_STREAM		_IOWR('W', 0, uint64_t)
#define SG_STREAM_RUNNING		_IOWR('W', 1, uint64_t)

enum {
	ERROR_REQUEST_RESPONSE = 0,
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

enum {
	CHANNEL_HOST = 0,
	CHANNEL_TPU0,
	CHANNEL_TPU1,
	CHANNEL_TPU2,
	CHANNEL_TPU3,
	CHANNEL_TPU4,
	CHANNEL_TPU5,
	CHANNEL_TPU6,
	CHANNEL_TPU7,
	CHANNEL_MEDIA0 = 9,
	CHANNEL_MEDIA31 = 40,
	CHANNEL_MAX,
};

enum TASK_TYPE {
	ERROR_TASK_TYPE = 0,
	TASK_S2D,
	TASK_D2S,
	TASK_D2D,
	LAUNCH_KERNEL,
	TRIGGER_TASK,
};

enum TASK_DESTINATION {
	TASK_TO_TP = 0,
	TASK_TO_AP,
	TASK_TO_RP,
};

enum TASK_RESP_REQUEST {
	TASK_NOT_NEED_RESP = 0,
	TASK_NEED_RESP,
};

struct cc_sys_info {
	uint32_t group_id;
	uint32_t block_id;
};

enum KERNEL_TYPE {
	NORMAL_KERNEL = 0,
	C2C_KERNEL,
	MAX_KERNEL,
};

enum {
	READ_NORMAL = 0,
	READ_REQUEST_HEAD = 1,
	READ_TASK = 2,
	READ_TPU_RESPONSE = 3,
	READ_MEDIA_RESPONSE = 4,
	READ_RESPONSE,
};
#define READ_TYPE_SHIFT	3
#define READ_TYPE	(0x7)
#define READ_TYPE_MASK	(~(0x7))

struct task_head {

	struct {
		uint8_t task_type;
		uint8_t task_dest;
		uint8_t task_resp;
		union {
			uint8_t cdma_mode;
			uint8_t kernel_type;
		};
		uint8_t reserved[4];
	};


	union {
		uint64_t task_id;
		uint64_t task_token;
	};

	union {
		uint64_t group_num;
		uint64_t src_addr;
	};
	union {
		uint64_t block_num;
		uint64_t dst_addr;
		struct {
			uint32_t block_index;
			uint32_t media_channel_index;
		};
	};
	union {
		struct cc_sys_info request_cc_info;
		uint64_t memcpy_size;
	};

	uint64_t stream_id;
	uint64_t task_body_size;
} __attribute__((packed));

struct task {
	struct task_head task_head;
	char task_body[0];
} __attribute__((packed));

struct time_stamp {
	union {
		uint64_t kr_time;
		uint64_t tmp_last_time;
		uint64_t verify_val;
	};
	uint64_t wait_resource_time;
	union {
		uint64_t ur_time;
		uint64_t tp_start_time;
		uint64_t wait_hw_avaliable;
	};
	uint64_t start_time;
	uint64_t end_time;
	uint64_t tp_end_time;
};

struct host_request_action {
	uint64_t request_id;
	uint32_t type;
	uint32_t subtype;
	union {
		uint64_t stream_id;
		uint64_t device_malloc_size;
		uint64_t free_list_num;
	};
	union {
		uint64_t task_id;
		uint64_t event_id;
		uint64_t callback_id;
	};
	union {
		uint64_t task_size;
		uint64_t record_stream_id;
	};
	struct time_stamp time;
} __attribute__((packed));

struct host_response_action {
	uint64_t response_id;
	uint32_t type;
	uint32_t subtype;
	union {
		uint64_t stream_id;
		uint64_t addr;
	};
	union {
		uint64_t task_id;
		uint64_t event_id;
		uint64_t callback_id;
	};
	struct time_stamp time;
	int result;
	int response_size;
	char response_body[0];
} __attribute__((packed));

struct record_response_action {
	struct list_head list;
	struct host_response_action response_body;
};

struct task_response_from_tpu {
	uint64_t stream_id;
	uint64_t task_id;
	uint32_t group_id;
	uint32_t block_id;
	uint64_t start_time;
	uint64_t end_time;
	uint64_t kr_time;
	uint64_t result;
} __attribute__((packed));

struct task_response_from_media {
	uint64_t stream_id;
	uint64_t task_id;
	uint32_t group_id;
	uint32_t block_id;
	uint64_t start_time;
	uint64_t end_time;
	uint64_t kr_time;
	uint64_t result;
	uint64_t response_size;
	char response_body[0];
} __attribute__((packed));

#endif
