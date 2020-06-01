#include <linux/kthread.h>

#include "probe_common.h"

extern u32 user_portid;
extern struct probe_queue  kprobe_q;

static char buff[LOG_SIZE];

int kprobe_thread(void *unused)
{
        hook_data_t *data = NULL;

        while (kthread_should_stop()) {
                if (queue_is_empty(&kprobe_q)) {
                        schedule();
                }

                data = kprobe_queue_get(&kprobe_q);
                snprintf(buff, LOG_SIZE, "%.16lld|%s", data->eclipse_time, data->task_com);
                send_netlink_msg(buff, LOG_SIZE, user_portid,
                                 0, HOOK_C_LOG, HOOK_A_LOG);

        }
}
