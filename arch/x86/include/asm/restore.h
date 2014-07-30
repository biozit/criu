#ifndef __CR_ASM_RESTORE_H__
#define __CR_ASM_RESTORE_H__

#define JUMP_TO_RESTORER_BLOB(new_sp, restore_task_exec_start,          \
			      task_args)				\
	asm volatile(							\
		     "movq %0, %%rbx				    \n" \
		     "movq %1, %%rax				    \n" \
		     "movq %2, %%rdi				    \n" \
		     "movq %%rbx, %%rsp			       	    \n"	\
		     "callq *%%rax				    \n" \
		     :							\
		     : "g"(new_sp),					\
		       "g"(restore_task_exec_start),			\
		       "g"(task_args)					\
		     : "rsp", "rdi", "rsi", "rbx", "rax", "memory")

#define core_get_tls(pcore, ptls)


int sigreturn_prep_fpu_frame(struct thread_restore_args *args, CoreEntry *core);

#endif
