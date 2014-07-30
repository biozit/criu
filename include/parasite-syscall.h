#ifndef __CR_PARASITE_SYSCALL_H__
#define __CR_PARASITE_SYSCALL_H__

#define BUILTIN_SYSCALL_SIZE	8

#include "pstree.h"

struct parasite_dump_thread;
struct parasite_dump_misc;
struct parasite_drain_fd;
struct vm_area_list;
struct pstree_item;
struct _CredsEntry;
struct _CoreEntry;
struct list_head;
struct cr_fdset;
struct fd_opts;
struct pid;

/* parasite control block */
struct parasite_ctl {
	struct pid		pid;
	void			*remote_map;
	void			*local_map;
	unsigned long		map_length;

	/* thread leader data */
	bool			daemonized;
	user_regs_struct_t	regs_orig;				/* original registers */

	k_rtsigset_t		sig_blocked;

	void			*rstack;				/* thread leader stack*/
	struct rt_sigframe	*sigframe;
	struct rt_sigframe	*rsigframe;				/* address in a parasite */

	void			*r_thread_stack;			/* stack for non-leader threads */

	unsigned long		parasite_ip;				/* service routine start ip */
	unsigned long		syscall_ip;				/* entry point of infection */
	u8			code_orig[BUILTIN_SYSCALL_SIZE];

	unsigned int		*addr_cmd;				/* addr for command */
	void			*addr_args;				/* address for arguments */
	unsigned long		args_size;
	int			tsock;					/* transport socket for transfering fds */

	struct list_head	pre_list;
	struct page_pipe	*mem_pp;
};

extern int parasite_dump_sigacts_seized(struct parasite_ctl *ctl, struct cr_fdset *cr_fdset);
extern int parasite_dump_itimers_seized(struct parasite_ctl *ctl, struct cr_fdset *cr_fdset);

struct proc_posix_timers_stat;
extern int parasite_dump_posix_timers_seized(struct proc_posix_timers_stat *proc_args, struct parasite_ctl *ctl, struct cr_fdset *cr_fdset);

#define parasite_args(ctl, type)					\
	({								\
		BUILD_BUG_ON(sizeof(type) > PARASITE_ARG_SIZE_MIN);	\
		ctl->addr_args;						\
	})

extern void *parasite_args_s(struct parasite_ctl *ctl, int args_size);
extern int parasite_send_fd(struct parasite_ctl *ctl, int fd);

/*
 * Execute a command in parasite when it's in daemon mode.
 * The __-ed version is asyncronous (doesn't wait for ack).
 */
extern int parasite_execute_daemon(unsigned int cmd, struct parasite_ctl *ctl);
extern int __parasite_execute_daemon(unsigned int cmd, struct parasite_ctl *ctl);

extern int __parasite_wait_daemon_ack(unsigned int cmd,
					      struct parasite_ctl *ctl);

extern int parasite_dump_misc_seized(struct parasite_ctl *ctl, struct parasite_dump_misc *misc);
extern int parasite_dump_creds(struct parasite_ctl *ctl, struct _CredsEntry *ce);
extern int parasite_dump_thread_seized(struct parasite_ctl *ctl, int id,
					struct pid *tid, struct _CoreEntry *core);
extern int dump_thread_core(int pid, CoreEntry *core, const struct parasite_dump_thread *dt);

extern int parasite_drain_fds_seized(struct parasite_ctl *ctl,
					struct parasite_drain_fd *dfds,
					int *lfds, struct fd_opts *flags);
extern int parasite_get_proc_fd_seized(struct parasite_ctl *ctl);

extern int parasite_cure_remote(struct parasite_ctl *ctl);
extern int parasite_cure_local(struct parasite_ctl *ctl);
extern int parasite_cure_seized(struct parasite_ctl *ctl);
extern struct parasite_ctl *parasite_infect_seized(pid_t pid,
						   struct pstree_item *item,
						   struct vm_area_list *vma_area_list,
						   struct parasite_drain_fd *dfds,
						   int timer_n);
extern struct parasite_ctl *parasite_prep_ctl(pid_t pid,
					      struct vm_area_list *vma_area_list);
extern int parasite_map_exchange(struct parasite_ctl *ctl, unsigned long size);

extern struct parasite_tty_args *parasite_dump_tty(struct parasite_ctl *ctl, int fd);

extern int parasite_init_threads_seized(struct parasite_ctl *ctl, struct pstree_item *item);
extern int parasite_fini_threads_seized(struct parasite_ctl *ctl);

extern int syscall_seized(struct parasite_ctl *ctl, int nr, unsigned long *ret,
			  unsigned long arg1, unsigned long arg2,
			  unsigned long arg3, unsigned long arg4,
			  unsigned long arg5, unsigned long arg6);

extern int __parasite_execute_syscall(struct parasite_ctl *ctl,
				user_regs_struct_t *regs);
extern bool arch_can_dump_task(pid_t pid);

extern int parasite_fixup_vdso(struct parasite_ctl *ctl, pid_t pid,
			       struct vm_area_list *vma_area_list);

extern int parasite_stop_on_syscall(int tasks, int sys_nr);
extern int parasite_unmap(struct parasite_ctl *ctl, unsigned long addr);
#endif /* __CR_PARASITE_SYSCALL_H__ */
