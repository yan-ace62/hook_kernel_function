#include <linux/ktime.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/list.h>

#include "probe_common.h"

extern struct kprobe_queue kprobe_log_queue;

struct ktime_data {
        ktime_t entry_stamp;
};


LIST_HEAD(func_name_list);

extern struct probe_queue k_queue;
extern struct kmem_cache *hook_data_cache;

struct kprobe_obj_node {
        struct list_head node;
        char* f_name;
        struct kretprobe *retprobe;
};

void insert_func_name_list(char *f_name)
{
        struct kprobe_obj_node *obj;
        obj = kmalloc(sizeof(*obj), GFP_KERNEL);
        obj->f_name = f_name;
        list_add(&func_name_list, &obj->node);
        return;
}

struct kprobe_obj_node* func_name_in_list(char *f_name)
{
        struct kprobe_obj_node *p;

        list_for_each_entry(p, &func_name_list, node) {
                if (strcmp(f_name, p->f_name)) {
                        return p;
                }
        }

        return NULL;
}


/* Here we use the entry_hanlder to timestamp function entry */
static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
        struct ktime_data *data;

        if (!current->mm)
                return 1;       /* Skip kernel threads */

        data = (struct ktime_data *)ri->data;
        data->entry_stamp = ktime_get();
        return 0;
}

/*
 * Return-probe handler: Log the return value and duration. Duration may turn
 * out to be zero consistently, depending upon the granularity of time
 * accounting on the platform.
 */
static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
        hook_data_t *node;
        int retval = regs_return_value(regs);
        struct ktime_data *data = (struct ktime_data *)ri->data;
        ktime_t now;

        now = ktime_get();

        node = kmem_cache_alloc(hook_data_cache, GFP_KERNEL);
        node->eclipse_time = ktime_to_ns(ktime_sub(now, data->entry_stamp));
        strncpy(node->task_com, current->comm, TASK_COMM_LEN);
        kprobe_queue_put(&kprobe_log_queue, node);

        return retval;
}

int do_kprobe_register(char func_name[])
{
        int ret;
        char *f_name;
        struct kretprobe *kret;
        struct kprobe_obj_node *obj;

        if (func_name_in_list(func_name)) {
                printk("hook function registed\n");
                return 0;
        }

        f_name = kstrdup(func_name, GFP_KERNEL);
        kret = kmalloc(sizeof(*kret), GFP_KERNEL);

        kret->handler = ret_handler;
        kret->entry_handler = entry_handler;
        kret->data_size = sizeof(struct ktime_data);
        kret->kp.symbol_name = f_name;

        ret = register_kretprobe(kret);
        if (!ret) {
                obj = kmalloc(sizeof(obj), GFP_KERNEL);
                obj->f_name = f_name;
                obj->retprobe = kret;
                list_add(&func_name_list, &obj->node);
        } else {
                printk(KERN_INFO "register_kretprobe failed, returned %d\n", ret);
        }

        return ret;
}

void do_kprobe_unregister(char func_name[])
{
        struct kprobe_obj_node *kret_obj;

        if ((kret_obj = func_name_in_list(func_name))) {
                list_del(&kret_obj->node);
                unregister_kretprobe(kret_obj->retprobe);

                kfree(kret_obj->f_name);
                kfree(kret_obj->retprobe);
                kfree(kret_obj);
        }

        return;
}