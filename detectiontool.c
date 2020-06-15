#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <asm/asm-offsets.h> /* NR_syscalls */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua Lim, Fai Yew");

// name of the detection tool's proc filesystem entry
// permissions of detection tool's proc filesystem entry
#define TOOL_PROCFS_ENTRYNAME "detectiontool"
#define TOOL_PROCFS_ENTRYPERM 0666

// commands
#define DETECTPID_CMD "detectpid"
#define DETECTFILE_CMD "detectfile"
#define DETECTHOOKS_CMD "detecthooks"
#define DETECTMODULES_CMD "detectmods"

// declarations
static unsigned int syscall_table_size;
static unsigned long *addr_syscall_table;
static unsigned long *dump_syscall_table;

// prototypes
static int tool_procfs_entry_init(void);
static ssize_t tool_procfs_write(struct file *, const char __user *, size_t, loff_t *);
static ssize_t tool_procfs_read(struct file *, char __user *, size_t, loff_t *);
static void hook_detection(void);
static void analyze_modules(void);
static struct proc_dir_entry *tool_procfs_entry;

// handlers for the read and write operations from/to tool's
// proc filesystem entry
static struct file_operations tool_procfs_fops =
	{
		.write = tool_procfs_write,
		.read = tool_procfs_read};

// create a proc filesystem entry for the detection tool
static int tool_procfs_entry_init(void)
{
	tool_procfs_entry = proc_create(TOOL_PROCFS_ENTRYNAME,
									TOOL_PROCFS_ENTRYPERM,
									NULL,
									&tool_procfs_fops);

	if (tool_procfs_entry == NULL)
		return 0;

	return 1;
}

static ssize_t tool_procfs_write(struct file *fp,
								 const char __user *ubuf,
								 size_t count,
								 loff_t *offp)
{
	char buf[16];
	memset(buf, 0x0, 16);

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	if (strcmp(buf, DETECTPID_CMD) == 0)
	{
		// detect hidden pids
	}

	else if (strcmp(buf, DETECTFILE_CMD) == 0)
	{
		// detect hidden files
	}

	else if (strcmp(buf, DETECTHOOKS_CMD) == 0)
	{
		// detect hooked functions
		memset(buf, 0x0, 16);
		hook_detection();
	}
	else if (strcmp(buf, DETECTMODULES_CMD) == 0)
	{
		// detect hidden modules
		memset(buf, 0x0, 16);
		analyze_modules();
	}

	return count;
}

static ssize_t tool_procfs_read(struct file *fp,
								char __user *buf,
								size_t count,
								loff_t *offset)
{

	const char tools_cmds[] =
		"#######################\n"
		"Detection Tool Commands\n"
		"#######################\n\n"
		"\t* [-p] -->> to detect hidden PIDs on the system\n"
		"\t* [-f] -->> to detect hidden files on the system\n"
		"\t* [-s] -->> to detect hooked functions\n"
		"\t* [-m] -->> to detect hidden modules\n"
		"\x00";

	if (copy_to_user(buf, tools_cmds, strlen(tools_cmds)))
		return -EFAULT;

	if (*offset != 0)
		return 0;

	*offset += 1;
	return (ssize_t)strlen(tools_cmds);
}

// detect hooked system call table
static unsigned long *get_syscalls_table(void)
{
	unsigned long *syscall_table;
	syscall_table = (unsigned long *)kallsyms_lookup_name("sys_call_table");
	return syscall_table;
}

static unsigned int get_size_syscalls_table(void)
{
	unsigned int size = 0;

	while (addr_syscall_table[size++])
		;
	return size * sizeof(unsigned long *);
}

static void hook_detection(void)
{
	unsigned int sys_num = 0;
	unsigned long addr;
	unsigned char test[12];
	struct file *fp;
	int no_of_syscall_hooks = 0;
	int no_of_fops_hooks = 0;
	int hooked_functions = 0;
	char path[6];

	// scan sys_call_table
	printk(KERN_INFO "detection tool: Scanning sys_call_table...\n");
	for (sys_num = 0; sys_num < NR_syscalls; sys_num++)
	{
		if (addr_syscall_table[sys_num] != dump_syscall_table[sys_num])
		{
			printk(KERN_ALERT "detection tool: Hook detected! (syscall %d)\n", sys_num);
			no_of_syscall_hooks++;
		}
		// detect inline hook with mov and jmp
		else
		{
			addr = addr_syscall_table[sys_num];
			memcpy(test, (void *)addr, 12);
			if (test[0] == 0x48 && test[1] == 0xb8 && test[10] == 0xff && test[11] == 0xe0)
			{
				printk(KERN_ALERT "detection tool: Hook detected! (syscall %d)\n", sys_num);
				no_of_syscall_hooks++;
			}
			// int j;
			// for (j = 0; j < 12; j++)
			// {
			// 	printk(KERN_INFO "test: %x\n", test[j]);
			// }
		}
	}
	if (no_of_syscall_hooks)
		printk(KERN_ALERT "detection tool: %d hooked system calls detected!\n", no_of_syscall_hooks);
	else
		printk(KERN_INFO "detection tool: No hooked system calls detected.\n");

	printk(KERN_INFO "detection tool: Scanning fops of /, /proc and /sys...\n");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
#define SCAN_FOPS(NAME)                                                             \
	sprintf(path, "/%s", #NAME);                                                    \
	fp = filp_open(path, O_RDONLY, S_IRUSR);                                        \
	if (IS_ERR(fp))                                                                 \
		printk(KERN_ERR "detection tool: Failed to open %s!\n", path);              \
	if (IS_ERR(fp->f_op))                                                           \
		printk(KERN_WARNING "detection tool: %s has no fops!\n", path);             \
                                                                                    \
	addr = (unsigned long)fp->f_op->iterate_shared;                                 \
	memcpy(test, (void *)addr, 12);                                                 \
	if (test[0] == 0x48 && test[1] == 0xb8 && test[10] == 0xff && test[11] == 0xe0) \
	{                                                                               \
		printk(KERN_ALERT "detection tool: %s fops hook detected!\n", path);        \
		no_of_fops_hooks++;                                                         \
	}
#else
#define SCAN_FOPS(NAME)                                                             \
	sprintf(path, "/%s", #NAME);                                                    \
	fp = filp_open(path, O_RDONLY, S_IRUSR);                                        \
	if (IS_ERR(fp))                                                                 \
		printk(KERN_ERR "detection tool: Failed to open %s!\n", path);              \
	if (IS_ERR(fp->f_op))                                                           \
		printk(KERN_WARNING "detection tool: %s has no fops!\n", path);             \
                                                                                    \
	addr = (unsigned long)fp->f_op->iterate;                                        \
	memcpy(test, (void *)addr, 12);                                                 \
	if (test[0] == 0x48 && test[1] == 0xb8 && test[10] == 0xff && test[11] == 0xe0) \
	{                                                                               \
		printk(KERN_ALERT "detection tool: %s fops hook detected!\n", path);        \
		no_of_fops_hooks++;                                                         \
	}
#endif

	SCAN_FOPS()
	SCAN_FOPS(proc)
	SCAN_FOPS(sys)

	if (no_of_fops_hooks)
		printk(KERN_ALERT "detection tool: %d hooked fops detected!\n", no_of_fops_hooks);
	else
		printk(KERN_INFO "detection tool: No hooked fops detected.\n");

	hooked_functions = no_of_fops_hooks + no_of_syscall_hooks;
	if (hooked_functions)
		printk(KERN_ALERT "detection tool: Scanning complete. A total of %d hooked functions on your system was detected.\n", hooked_functions);
	else
		printk(KERN_INFO "detection tool: Scanning complete. No hooked functions on your system were detected.\n");
}

// detect hidden modules
static void analyze_modules(void)
{
	struct kset *mod_kset;
	struct kobject *cur, *tmp;
	struct module_kobject *kobj;
	int modulesFound = 0;

	printk(KERN_INFO "detection tool: Scanning Modules...\n");

	mod_kset = (void *)kallsyms_lookup_name("module_kset");
	if (!mod_kset)
		return;

	list_for_each_entry_safe(cur, tmp, &mod_kset->list, entry)
	{
		if (!kobject_name(tmp))
			break;

		kobj = container_of(tmp, struct module_kobject, kobj);

		if (kobj && kobj->mod && kobj->mod->name)
		{
			mutex_lock(&module_mutex);
			if (!find_module(kobj->mod->name))
			{
				printk(KERN_ALERT "detection tool: Module [%s] hidden.\n", kobj->mod->name);
				modulesFound++;
			}
			mutex_unlock(&module_mutex);
		}
	}

	if (modulesFound)
		printk(KERN_ALERT "detection tool: Scanning complete. A total of %d hidden modules on your system was detected.\n", modulesFound);
	else
		printk(KERN_INFO "detection tool: Scanning complete. No hidden modules on your system were detected.\n");
}

static int tool_init(void)
{
	if (!tool_procfs_entry_init())
		return -1;

	addr_syscall_table = get_syscalls_table();
	if (!addr_syscall_table)
	{
		printk(KERN_INFO "detection tool: Failed - Address of syscalls table not found\n");
		return -ECANCELED;
	}

	syscall_table_size = get_size_syscalls_table();
	dump_syscall_table = kmalloc(syscall_table_size, GFP_KERNEL);
	if (!dump_syscall_table)
	{
		printk(KERN_INFO "detection tool: Failed - Not enough memory\n");
		return -ENOMEM;
	}
	memcpy(dump_syscall_table, addr_syscall_table, syscall_table_size);

	printk(KERN_INFO "detection tool: Init OK\n");
	return 0;
}

static void tool_exit(void)
{
	proc_remove(tool_procfs_entry);
	kfree(dump_syscall_table);
	printk(KERN_INFO "detection tool: Exiting\n");
}

module_init(tool_init);
module_exit(tool_exit);