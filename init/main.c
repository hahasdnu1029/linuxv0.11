/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
// 0X90002指针首地址这个内存地址存储的扩展内存值（单位为KB）(unsigned short类型，*取值)
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

// 从主板上的一个小的CMOS中提取开机时间的信息组成开机时间startup_time记录在内核数据区
static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	// 根据内核数据区的机器系统数据设置根设备号和硬盘信息
 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	 // 根据实际的物理内存来规划内存（缓冲区、主内存、虚拟盘）
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	// 按位与,1的位置显示本身，0的位置为0。这里保证实际的物理内存大小为1页（4K）的整数倍
	memory_end &= 0xfffff000;
	// 如果大于16M,按照16M。因为这个版本的linux的内存大小为16M
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	// 如果大于12M缓冲区大小设置为4M
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	// 如果大于6M缓冲区大小设置为2M
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	// 反之缓冲区设置为1M 
	else
		buffer_memory_end = 1*1024*1024;
	// 主内存开始的位置就是就是缓冲区结束的位置
	main_memory_start = buffer_memory_end;
//这只虚拟盘 这个版本的linux的虚拟盘大小为2M，调用的rd_init进行虚拟盘的设置。
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	// 调用mem_init函数进行主内存管理结构的初始化,  对于本linux版本main_memory_start为6M处，main_end为16M处。
	mem_init(main_memory_start,memory_end);
	// 将异常处理类中断服务程序和IDT进行挂接。IDT表项0~255（256项）挂接0~47（48项）
	trap_init();
	// 块设备初始化，首先linux将外设分为两类：块设备和字符设备，进程要与块设备进行数据交换必须经过内存的缓冲区，初始化块设备的请求项数据结构，该结构记录了
	// 内存缓冲块与外设逻辑块之间的读写关系：即逻辑块哪个需要读到缓冲区，需要将缓冲区的那个块写到逻辑的哪个块。
	blk_dev_init();
	chr_dev_init();
	// 对字符设备进行初始化（建立人机交互，将字符设备的中断服务程序与IDT挂接）
	tty_init();
	// 开机时间的设置
	time_init();
	// sched_init主要做了3件事，如下：
	// 1、初始化进程0
	// 2、设置时钟中断，因为此时的linux系统是支持多任务的操作系统，所以需要设置时钟中断来支持多进程的轮询。
	// 3、让进程须有系统调用能力（进程在运行过程中需要与内核进行交互，交互的端口就是系统调用程序，通过set_system_gate将sysetm_call系统调用函数与IDT挂接）
	sched_init();
	// 初始化缓冲区管理结构
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti();
	move_to_user_mode();
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

void init(void)
{
	int pid,i;

	setup((void *) &drive_info);
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
