#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"
#include <mips/trapframe.h>
#if OPT_A2

#include <synch.h>
#include <limits.h>
#include <vfs.h>
#include <kern/fcntl.h>

static void entryptfn(void *a, unsigned long b){
	(void) b;
	struct trapframe tf = *(struct trapframe *) a; 
	kfree(a);
	tf.tf_v0 = 0;
	tf.tf_a3 = 0;
	tf.tf_epc += 4;
	/* kprintf(" thread created\n"); */
	mips_usermode(&tf);
}
int sys_fork(struct trapframe* tf, pid_t* retval){

	lock_acquire(proc_lock);
	struct proc * child = proc_create_runprogram(curproc->p_name);
	if (child == NULL) {
		lock_release(proc_lock);
		return ENOMEM;
	}
	child->parent_dead = false;

	struct addrspace* child_addr;
	int copy_error = as_copy(curproc->p_addrspace, &child_addr);
	if (copy_error) {
		proc_destroy(child);
		lock_release(proc_lock);
		return copy_error;
	}
	
	struct trapframe *frame = kmalloc(sizeof(struct trapframe));
	if (frame == NULL){
		as_destroy(child_addr);
		proc_destroy(child);
		lock_release(proc_lock);
		return ENOMEM;
	}

	int array_error = array_add(curproc->childlst, child, NULL);
	if(array_error != 0) {
		kfree(frame);
		as_destroy(child_addr);
		proc_destroy(child);
		lock_release(proc_lock);
		return array_error;
	}
		
	*frame = *tf;
	int thread_error = thread_fork(curproc->p_name, child,entryptfn,frame,0); 
	if (thread_error) {
		array_remove(curproc->childlst, curproc->childlst->num-1); 
		kfree(frame);
		as_destroy(child_addr);
		proc_destroy(child);
		lock_release(proc_lock);
		return thread_error;
	}
	spinlock_acquire(&child->p_lock);
	child->p_addrspace = child_addr;
	spinlock_release(&child->p_lock);
	*retval = child->pid;

	lock_release(proc_lock);
	return 0;
}

#endif

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
#if OPT_A2

	lock_acquire(proc_lock);
#endif
	struct addrspace *as;
	struct proc *p = curproc;
#if OPT_A2

	DEBUG(DB_SYSCALL, "proc %d exit.\n", p->pid);
	// notify child that I died.
	for(unsigned int i = 0; i < p->childlst->num; ++i) {
		struct proc* temp = (struct proc*)array_get(p->childlst,i);
		if (temp->dead){
			proc_destroy(temp);
		} else {
			temp->parent_dead = true;
		}
	}
	p->exitcode = exitcode;
	cv_broadcast(p->wait_cv, proc_lock);
	p->dead = true;



#endif
	/* for now, just include this to keep the compiler from complaining about
	   an unused variable */

	DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

	KASSERT(curproc->p_addrspace != NULL);
	as_deactivate();
	/*
	 * clear p_addrspace before calling as_destroy. Otherwise if
	 * as_destroy sleeps (which is quite possible) when we
	 * come back we'll be calling as_activate on a
	 * half-destroyed address space. This tends to be
	 * messily fatal.
	 */
	as = curproc_setas(NULL);
	as_destroy(as);

	/* detach this thread from its process */
	/* note: curproc cannot be used after this call */
	proc_remthread(curthread);

	/* if this is the last user process in the system, proc_destroy()
	   will wake up the kernel menu thread */
#if OPT_A2
	if (p->parent_dead){
		proc_destroy(p);
	}
	lock_release(proc_lock);
#else
	proc_destroy(p);
#endif 
	thread_exit();
	/* thread_exit() does not return, so we should never get here */
	panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
	*retval = curproc->pid;
#else
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
#if OPT_A2
	lock_acquire(proc_lock);
	struct proc* child = NULL;
	for(unsigned int i = 0; i < array_num(curproc->childlst); ++i) {
		struct proc* temp = (struct proc*)array_get(curproc->childlst,i);
		if (temp->pid == pid){
			child = temp;
			array_remove(curproc->childlst, i);
			break;
		}
	}
	if (child == NULL) {
		lock_release(proc_lock);
		return ECHILD; 
	}
	if(! child->dead) {
		cv_wait(child->wait_cv, proc_lock);
	}

	exitstatus = _MKWAIT_EXIT(child->exitcode);
	proc_destroy(child);

	lock_release(proc_lock);
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
static void clean_up_kargs(char ** k_args, int until){

	/* destory k_args */
	for(int it = 0; it < until; ++it){
		kfree(k_args[it]);
	}
	kfree(k_args);
}

int sys_execv(userptr_t program, userptr_t args){

	int result;
	/* count args and cp to kernel */
	int nargs = 0;
	char ** k_args= kmalloc(sizeof(char*) * ARG_MAX/PATH_MAX);
	for(int i= 0; i < PATH_MAX; ++i){
		userptr_t dest;
		result = copyin(args, &dest, sizeof(userptr_t)); 	
		if(result){
			clean_up_kargs(k_args, i);
			return result;
		}
		if(dest == NULL) {
			break;
		}
		k_args[i] = kmalloc(sizeof(char) * PATH_MAX);
		result = copyinstr(dest, k_args[i], PATH_MAX, NULL);
		if(result){
			clean_up_kargs(k_args, i+1);
			return result;
		}

		nargs++;
		args += sizeof(userptr_t);
	}

	/* copy the progam path */
	char *prognName = kmalloc(PATH_MAX);
	result = copyinstr(program, prognName,PATH_MAX,NULL);
	if(result){
		clean_up_kargs(k_args, nargs);
		kfree(prognName);
		return result;
	}




	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	/* Open the file. */
	result = vfs_open(prognName, O_RDONLY, 0, &v);
	if (result) {
		clean_up_kargs(k_args, nargs);
		kfree(prognName);
		vfs_close(v);
		return result;
	}


	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		clean_up_kargs(k_args, nargs);
		kfree(prognName);
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	struct addrspace *oldas = curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		clean_up_kargs(k_args, nargs);
		kfree(prognName);
		vfs_close(v);
		as_deactivate();
		curproc_setas(oldas);
		as_destroy(as);
		as_activate();
		return result;
	}
	/* set flag to loaded */
	as->as_loaded = true;

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		clean_up_kargs(k_args, nargs);
		kfree(prognName);
		as_deactivate();
		curproc_setas(oldas);
		as_destroy(as);
		as_activate();
		return result;
	}
	/* copy args to kernal stack */
	userptr_t nul = NULL;
	stackptr = stackptr - sizeof(userptr_t);
	result = copyout(&nul, (userptr_t)stackptr,sizeof(userptr_t));

	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		clean_up_kargs(k_args, nargs);
		kfree(prognName);
		as_deactivate();
		curproc_setas(oldas);
		as_destroy(as);
		as_activate();
		return result;
	}
	/* calculate the reversed space */
	vaddr_t user_args = stackptr - (sizeof (userptr_t) * nargs);
	vaddr_t newentrypoint = stackptr - (sizeof(userptr_t) * nargs);
	for (int i = nargs-1; i >= 0; --i){
		int len = strlen(k_args[i]) + 1;
		newentrypoint -= len;
		result = copyoutstr(k_args[i], (userptr_t)newentrypoint, len, NULL);
		if (result) {
			/* p_addrspace will go away when curproc is destroyed */
			clean_up_kargs(k_args, nargs);
			kfree(prognName);
			as_deactivate();
			curproc_setas(oldas);
			as_destroy(as);
			as_activate();
			return result;
		}
		stackptr = stackptr - sizeof(userptr_t);
		result = copyout(&newentrypoint, (userptr_t)stackptr, sizeof(userptr_t));
		if (result) {
			/* p_addrspace will go away when curproc is destroyed */
			clean_up_kargs(k_args, nargs);
			kfree(prognName);
			as_deactivate();
			curproc_setas(oldas);
			as_destroy(as);
			as_activate();
			return result;
		}
	}
	stackptr = newentrypoint;
	stackptr = ROUNDDOWN(stackptr, 8);
		

	/* destory addrspace */
	as_destroy(oldas);

	clean_up_kargs(k_args, nargs);
	kfree(prognName);
	/* Warp to user mode. */
	enter_new_process(nargs, (userptr_t)user_args,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;


}
#endif
