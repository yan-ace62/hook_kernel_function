#include <linux/kthread.h>
#include <linux/slab.h>

#include "probe_common.h"
#include "kprobe_genetlink.h"

s32 user_portid = -1;
extern struct kprobe_queue kprobe_log_queue;

static char buffs[LOG_SIZE * 11];

void kprobe_queue_send(struct kprobe_queue *queue)
{
        int count = kprobe_queue_number(queue);
        int index;
        int number = count > 10 ? 10 : count;
        hook_data_t *data = NULL;

        memset(buffs, 0, sizeof(buffs));
        for (index = 0; index < number; index++) {
                data = kprobe_queue_get(&kprobe_log_queue);
                snprintf(buffs+(index * LOG_SIZE), LOG_SIZE, "%.16lld|%s,",
                        data->eclipse_time, data->task_com);
                kfree(data);
        }

        if (index > 0) {
                send_netlink_msg(buffs, LOG_SIZE * (index + 1), user_portid, 0, HOOK_C_LOG, HOOK_A_LOG);
        }
}


int kprobe_thread(void *unused)
{
        printk("Kthread start\n");
        while (!kthread_should_stop()) {
                if (kprobe_queue_is_empty(&kprobe_log_queue)) {
                        schedule();
                }

                kprobe_queue_send(&kprobe_log_queue);
        }
        printk("Kthread end\n");

        return 0;
}
