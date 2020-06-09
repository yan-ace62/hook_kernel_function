#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/genhd.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <crypto/hash.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "csdata.h"
#include "cshook.h"
#include "cstask.h"
#include "csnetlink.h"
#include "cskernel.h"

#define CR0_WRITE_PROTECT 0x10000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cloudscreen");
MODULE_DESCRIPTION("Cloudscreen uds agent hook");

sys_read_t old_read = NULL;
sys_write_t old_write = NULL;
sys_sendto_t old_sendto = NULL;
sys_sendfile_t old_sendfile = NULL;
sys_close_t old_close = NULL;
sys_unlink_t old_unlink = NULL;
sys_unlinkat_t old_unlinkat = NULL;
sys_link_t old_link = NULL;
sys_linkat_t old_linkat = NULL;
sys_rename_t old_rename = NULL;
sys_renameat_t old_renameat = NULL;
sys_renameat2_t old_renameat2 = NULL;
sys_profile_task_exit_t old_profile_task_exit = NULL;
sys_splice_t old_splice = NULL;
sys_kill_t old_kill = NULL;
sys_creat_t old_creat = NULL;
sys_open_t old_open = NULL;
sys_openat_t old_openat = NULL;
kernel_get_cmdline_t kernel_get_cmdline = NULL;
kernel_absolute_path_t kernel_absolute_path = NULL;
sys_writev_t old_writev = NULL;

static struct task_struct *dbsave_task = NULL;
static struct task_struct *rename_task = NULL;
static uintptr_t *syscall = NULL;
static uintptr_t profile_task_exit_call_addr = 0;
//static uintptr_t reboot_pid_ns_call_addr = 0;

void wakeup_rename_task(void) 
{
	wake_up_process(rename_task);
	return;
}

uintptr_t get_system_symbol_addr(const char *symbol)
{
	uintptr_t addr = 0;
	const char *kallsyms_path = "/proc/kallsyms";
	struct file *fp = filp_open(kallsyms_path, O_RDONLY, 0);
	if (IS_ERR(fp)){
		printk(KERN_INFO "Can not open %s\n",kallsyms_path);
		return 0;
	}
	mm_segment_t fs = get_fs();
	set_fs(KERNEL_DS);
	const size_t LINE_SIZE = 255;
	char line[LINE_SIZE];
	int i=0;
	loff_t pos=0;
	while(i<LINE_SIZE && vfs_read(fp, line + i, 1, &pos) == 1){
		if (line[i]=='\n'){
			if (!strncmp(line+16, symbol, strlen(symbol))){
				line[16] = 0;
				addr = hex2ptr(line);
				printk(KERN_INFO "address %lx,%s", addr, symbol);
				break;
			}else{
				i = 0;
				continue;
			}
		}
		i++;
	}
	set_fs(fs);
	filp_close(fp, NULL);
	return addr;
}

static uint64_t get_cr0(void)
{
    uint64_t ret;

    __asm__ volatile (
        "movq %%cr0, %[ret]"
        : [ret] "=r" (ret)
    );
    return ret;
}

static void set_cr0(uint64_t cr0)
{
    __asm__ volatile (
        "movq %[cr0], %%cr0"
        :
        : [cr0] "r" (cr0)
    );
}

uintptr_t get_api_call_addr(uintptr_t parent_api_addr, uintptr_t api_addr, int range)
{
	uint8_t *pos = (uint8_t *)parent_api_addr;
	while(range>0){
		if (*pos==0xe8){
			int64_t offset = *(int32_t *)(pos+1);
			uintptr_t dest_addr = (uintptr_t)pos + 5 + offset;
			if (dest_addr == api_addr) return (uintptr_t)(pos+1);
		}
		pos++;
		range--;
	}
	return 0;
}

void set_call_addr(uintptr_t call_addr, uintptr_t api_addr)
{
	int32_t offset = (int32_t)(api_addr - call_addr - 4);
    set_cr0(get_cr0() & ~CR0_WRITE_PROTECT);
	*(int32_t *)call_addr = offset;	
    set_cr0(get_cr0() | CR0_WRITE_PROTECT);
}

uintptr_t set_hook(int syscall_id, uintptr_t hookfunc)
{
	uintptr_t oldfunc = syscall[syscall_id];
    set_cr0(get_cr0() & ~CR0_WRITE_PROTECT);
	syscall[syscall_id] = hookfunc;	
    set_cr0(get_cr0() | CR0_WRITE_PROTECT);
	return oldfunc;
}

int load_module(void)
{
	printk(KERN_INFO "cloudscreen module is loaded\n");
	printk("%s (pid=%d, comm=%s, ppid=%d, pcomm=%s)\n", __func__, current->pid, current->comm, current->real_parent->pid, current->real_parent->comm);
	mutex_init(&new_node_mutex);
	rename_jobs_list_init();
	cs_pidhash_init();
	cs_pathhash_init();
	csnetlink_init();
	dbsave_task = kthread_create(dbsave_thread, NULL, "cs_dbsave_thread");
	if (dbsave_task) wake_up_process(dbsave_task);
	rename_task = kthread_create(rename_dir_thread, NULL, "cs_rename_dir_thread");
	if (rename_task) wake_up_process(rename_task); else printk("rename_task failed\n");
	char md5[16];
	strmd5("/tmp/demo1.c", md5);
    insert_path_node(md5, 100, 0);
	strmd5("/tmp/demo2.c", md5);
    insert_path_node(md5, 100, 0);

	kernel_get_cmdline = (kernel_get_cmdline_t)get_system_symbol_addr(" T get_cmdline\n");
	kernel_absolute_path = (kernel_absolute_path_t)get_system_symbol_addr(" T d_absolute_path\n");
	old_profile_task_exit = (sys_profile_task_exit_t)get_system_symbol_addr(" T profile_task_exit\n");
	uintptr_t do_exit_addr = get_system_symbol_addr(" T do_exit\n");
	profile_task_exit_call_addr = get_api_call_addr(do_exit_addr, (uintptr_t)old_profile_task_exit, 0x100);
	set_call_addr(profile_task_exit_call_addr, (uintptr_t)cs_profile_task_exit);
	/*
	old_reboot_pid_ns = (reboot_pid_ns_t)get_system_symbol_addr(" T reboot_pid_ns\n");
	uintptr_t sys_reboot_addr =  get_system_symbol_addr(" t SYSC_reboot\n");
	reboot_pid_ns_call_addr = get_api_call_addr(sys_reboot_addr, (uintptr_t)old_reboot_pid_ns, 0x100);
	set_call_addr(reboot_pid_ns_call_addr, (uintptr_t)cs_reboot_pid_ns);
	*/
	syscall = (uintptr_t *) get_system_symbol_addr(" R sys_call_table\n");
	old_close = (sys_close_t)set_hook(__NR_close, (uintptr_t)cs_close);
	old_write = (sys_write_t)set_hook(__NR_write, (uintptr_t)cs_write);
	old_sendto = (sys_sendto_t)set_hook(__NR_sendto, (uintptr_t)cs_sendto);
	old_sendfile = (sys_sendfile_t)set_hook(__NR_sendfile, (uintptr_t)cs_sendfile);
	old_read = (sys_read_t)set_hook(__NR_read, (uintptr_t)cs_read);
	old_unlink = (sys_unlink_t)set_hook(__NR_unlink, (uintptr_t)cs_unlink);
	old_unlinkat = (sys_unlinkat_t)set_hook(__NR_unlinkat, (uintptr_t)cs_unlinkat);
	old_link = (sys_link_t)set_hook(__NR_link, (uintptr_t)cs_link);
	old_linkat = (sys_linkat_t)set_hook(__NR_linkat, (uintptr_t)cs_linkat);
	old_rename = (sys_rename_t)set_hook(__NR_rename, (uintptr_t)cs_rename);
	old_renameat = (sys_renameat_t)set_hook(__NR_renameat, (uintptr_t)cs_renameat);
	old_renameat2 = (sys_renameat2_t)set_hook(__NR_renameat2, (uintptr_t)cs_renameat2);
	old_splice = (sys_splice_t)set_hook(__NR_splice, (uintptr_t)cs_splice);
	old_kill = (sys_kill_t)set_hook(__NR_kill, (uintptr_t)cs_kill);
	old_creat = (sys_creat_t)set_hook(__NR_creat, (uintptr_t)cs_creat);
 	old_open = (sys_open_t)set_hook(__NR_open, (uintptr_t)cs_open);
 	old_openat = (sys_openat_t)set_hook(__NR_openat, (uintptr_t)cs_openat);
	old_writev = (sys_writev_t)set_hook(__NR_writev, (uintptr_t)cs_writev);
	printk("hook sendto:%lx, write:%lx, read:%lx, close:%lx, unlink:%lx\n", syscall[__NR_sendto], syscall[__NR_write], syscall[__NR_read], syscall[__NR_close], syscall[__NR_unlink]);
	printk("origin open:%p, creat:%p, openat:%p, close:%p \n", old_open, old_creat, old_openat, old_close);
	printk("       write:%p, read:%p, sendto:%p, sendfile:%p, splice:%p\n", old_write, old_read, old_sendto, old_sendfile, old_splice);
	printk("       unlink:%p, unlinkat:%p, link:%p, linkat:%p \n", old_unlink, old_unlinkat, old_link, old_linkat);
	printk("       rename:%p, renameat:%p, renameat2:%p, kill:%p\n", old_rename, old_renameat, old_renameat2, old_kill);
	return 0;
}
void unload_module(void)
{
	set_hook(__NR_read, (uintptr_t)old_read);
	set_hook(__NR_write, (uintptr_t)old_write);
	set_hook(__NR_sendto, (uintptr_t)old_sendto);
	set_hook(__NR_sendfile, (uintptr_t)old_sendfile);
	set_hook(__NR_close, (uintptr_t)old_close);
	set_hook(__NR_unlink, (uintptr_t)old_unlink);
	set_hook(__NR_unlinkat, (uintptr_t)old_unlinkat);
	set_hook(__NR_link, (uintptr_t)old_link);
	set_hook(__NR_linkat, (uintptr_t)old_linkat);
	set_hook(__NR_rename, (uintptr_t)old_rename);
	set_hook(__NR_renameat, (uintptr_t)old_renameat);
	set_hook(__NR_renameat2, (uintptr_t)old_renameat2);
	set_call_addr(profile_task_exit_call_addr, (uintptr_t)old_profile_task_exit);
	set_hook(__NR_splice, (uintptr_t)old_splice);
	set_hook(__NR_kill, (uintptr_t)old_kill);
	set_hook(__NR_creat, (uintptr_t)old_creat);
 	set_hook(__NR_open, (uintptr_t)old_open);
 	set_hook(__NR_openat, (uintptr_t)old_openat);
	//set_call_addr(reboot_pid_ns_call_addr, (uintptr_t)old_reboot_pid_ns);
	set_hook(__NR_writev, (uintptr_t)old_writev);
	csnetlink_exit();
	if (dbsave_task) kthread_stop(dbsave_task);
	if (rename_task) kthread_stop(rename_task);
	cs_pathhash_free();
	cs_pidhash_free();
	rename_jobs_list_free();
	printk(KERN_INFO "cloudscreen module is unloaded\n");
}

module_init(load_module);
module_exit(unload_module);
