#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "kprobe_genetlink.h"
#include "probe_common.h"

MODULE_LICENSE("GPL");

extern int kprobe_thread(void *unused);
extern s32 user_portid;

struct kmem_cache *hook_data_cache = NULL;
EXPORT_SYMBOL_GPL(hook_data_cache);
struct task_struct *thread_id = NULL;
struct kprobe_queue kprobe_log_queue;
EXPORT_SYMBOL_GPL(kprobe_log_queue);


static int __init kprobe_init(void)
{
        int ret = 0;

        ret = kprobe_netlink_init();

        if (ret) {
                printk("register gefalily error\n");
                return -1;
        }

        hook_data_cache = kmem_cache_create("hooks_data_cache",
                                            sizeof(hook_data_t),
                                            0, 0, NULL);

        kprobe_queue_init(&kprobe_log_queue);

        printk("start create kthread before");
        thread_id = kthread_run(kprobe_thread, NULL, "hook_function");
        printk("start create kthread after: %p", thread_id);

        if (IS_ERR(thread_id)) {
                printk("Create kthread error\n");
                return -1;
        }
        printk("Kprobe module init\n");

        return 0;
}

static void __exit kprobe_exit(void)
{
        int ret;

        ret = kprobe_netlink_exit();

        ret = kthread_stop(thread_id);
        kprobe_queue_destroy(&kprobe_log_queue);
        printk("start create kthread after: %p", thread_id);

        printk("Kprobe module exit\n");

        return;
}

module_init(kprobe_init);
module_exit(kprobe_exit);
