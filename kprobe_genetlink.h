#include <linux/sched.h>

#define LOG_SIZE (9 + TASK_COMM_LEN)

enum {
	HOOK_C_UNSPEC = 0,
	HOOK_C_FUNC_REGISTER,
	HOOK_C_FUNC_UNREGISTER,
        HOOK_C_LOG,
	__HOOK_C_MAX,
};

#define HOOK_C_MAX (__HOOK_C_MAX - 1)

enum {
	HOOK_A_UNSPEC = 0,
	HOOK_A_FUNC_NAME,
	HOOK_A_LOG,
	__HOOK_A_MAX,
};

#define HOOK_A_MAX (__HOOK_A_MAX - 1)

int send_netlink_msg(void *buf, int size, int portid, int seq, int cmd, int attr);
