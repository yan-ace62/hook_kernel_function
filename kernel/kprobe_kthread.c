#include <linux/kthread.h>

#include "probe_common.h"
#include "kprobe_genetlink.h"

s32 user_portid = -1;
extern struct kprobe_queue kprobe_log_queue;

static char buff[LOG_SIZE];

int kprobe_thread(void *unused)
{
        hook_data_t *data = NULL;

        while (kthread_should_stop()) {
                if (queue_is_empty(&kprobe_log_queue)) {
                        schedule();
                }

                data = kprobe_queue_get(&kprobe_log_queue);
                snprintf(buff, LOG_SIZE, "%.16lld|%s", data->eclipse_time, data->task_com);
                send_netlink_msg(buff, LOG_SIZE, user_portid,
                                 0, HOOK_C_LOG, HOOK_A_LOG);

        }

        return 0;
}
