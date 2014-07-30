#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "asm/types.h"
#include "list.h"
#include "namespaces.h"
#include "compiler.h"
#include "crtools.h"
#include "util.h"
#include "sockets.h"
#include "image.h"
#include "uts_ns.h"
#include "ipc_ns.h"
#include "pstree.h"
#include "cr-show.h"

#include "protobuf.h"
#include "protobuf/pstree.pb-c.h"
#include "protobuf/pipe-data.pb-c.h"
#include "protobuf/siginfo.pb-c.h"

#define DEF_PAGES_PER_LINE	6


static LIST_HEAD(pstree_list);

static void pipe_data_handler(int fd, void *obj)
{
	PipeDataEntry *e = obj;
	print_image_data(fd, e->bytes, opts.show_pages_content);
}

static int nice_width_for(unsigned long addr)
{
	int ret = 3;

	while (addr) {
		addr >>= 4;
		ret++;
	}

	return ret;
}

static inline void pr_xdigi(unsigned char *data, size_t len, int pos)
{
	if (pos < len)
		pr_msg("%02x ", data[pos]);
	else
		pr_msg("   ");
}

static inline void pr_xsym(unsigned char *data, size_t len, int pos)
{
	char sym;

	if (pos < len)
		sym = data[pos];
	else
		sym = ' ';

	pr_msg("%c", isprint(sym) ? sym : '.');
}

void print_data(unsigned long addr, unsigned char *data, size_t size)
{
	int i, j, addr_len;
	unsigned zero_line = 0;

	addr_len = nice_width_for(addr + size);

	for (i = 0; i < size; i += 16) {
		if (*(u64 *)(data + i) == 0 && *(u64 *)(data + i + 8) == 0) {
			if (zero_line == 0)
				zero_line = 1;
			else {
				if (zero_line == 1) {
					pr_msg("*\n");
					zero_line = 2;
				}

				continue;
			}
		} else
			zero_line = 0;

		pr_msg("%#0*lx: ", addr_len, addr + i);
		for (j = 0; j < 8; j++)
			pr_xdigi(data, size, i + j);
		pr_msg(" ");
		for (j = 8; j < 16; j++)
			pr_xdigi(data, size, i + j);

		pr_msg(" |");
		for (j = 0; j < 8; j++)
			pr_xsym(data, size, i + j);
		pr_msg(" ");
		for (j = 8; j < 16; j++)
			pr_xsym(data, size, i + j);

		pr_msg("|\n");
	}
}

void print_image_data(int fd, unsigned int length, int show)
{
	void *data;
	int ret;

	if (!show) {
		lseek(fd, length, SEEK_CUR);
		return;
	}

	pr_msg("\n");

	data = xmalloc(length);
	if (!data)
		return;
	ret = read_img_buf(fd, (unsigned char *)data, length);
	if (ret < 0) {
		xfree(data);
		return;
	}
	print_data(0, (unsigned char *)data, length);
	xfree(data);
}

static void show_pagemaps(int fd, void *obj)
{
	pb_show_plain_pretty(fd, PB_PAGEMAP, "nr_pages:%u");
}

void show_siginfo(int fd)
{
	int ret;

	pr_img_head(CR_FD_SIGNAL);
	while (1) {
		SiginfoEntry *sie;
		siginfo_t *info;

		ret = pb_read_one_eof(fd, &sie, PB_SIGINFO);
		if (ret <= 0)
			break;

		info = (siginfo_t *) sie->siginfo.data;
		pr_msg("signal: si_signo=%d si_code=%x\n",
				info->si_signo, info->si_code);
		siginfo_entry__free_unpacked(sie, NULL);

	}
	pr_img_tail(CR_FD_SIGNAL);
}

static int pstree_item_from_pb(PstreeEntry *e, struct pstree_item *item)
{
	int i;

	item->pid.virt = e->pid;
	item->nr_threads = e->n_threads;
	item->threads = xzalloc(sizeof(struct pid) * e->n_threads);
	if (!item->threads)
		return -1;

	for (i = 0; i < item->nr_threads; i++)
		item->threads[i].virt = e->threads[i];

	return 0;
}

static void pstree_handler(int fd, void *obj)
{
	PstreeEntry *e = obj;
	struct pstree_item *item = NULL;

	item = xzalloc(sizeof(struct pstree_item));
	if (!item)
		return;

	if (pstree_item_from_pb(e, item)) {
		xfree(item);
		return;
	}

	list_add_tail(&item->sibling, &pstree_list);
}

void show_collect_pstree(int fd, int collect)
{
	pb_show_plain_payload_pretty(fd, PB_PSTREE,
			collect ? pstree_handler : NULL, "*:%d");
}

static inline char *task_state_str(int state)
{
	switch (state) {
	case TASK_ALIVE:
		return "running/sleeping";
	case TASK_DEAD:
		return "zombie";
	default:
		return "UNKNOWN";
	}
}

static void show_core_regs(UserX86RegsEntry *regs)
{
#define pr_regs4(s, n1, n2, n3, n4)	\
	pr_msg("\t%8s: 0x%-16"PRIx64" "	\
	       "%8s: 0x%-16"PRIx64" "	\
	       "%8s: 0x%-16"PRIx64" "	\
	       "%8s: 0x%-16"PRIx64"\n",	\
	       #n1, s->n1,		\
	       #n2, s->n2,		\
	       #n3, s->n3,		\
	       #n4, s->n4)

#define pr_regs3(s, n1, n2, n3)		\
	pr_msg("\t%8s: 0x%-16"PRIx64" "	\
	       "%8s: 0x%-16"PRIx64" "	\
	       "%8s: 0x%-16"PRIx64"\n",	\
	       #n1, s->n1,		\
	       #n2, s->n2,		\
	       #n3, s->n3)

	pr_msg("\t---[ GP registers set ]---\n");

	pr_regs4(regs, cs, ip, ds, es);
	pr_regs4(regs, ss, sp, fs, gs);
	pr_regs4(regs, di, si, dx, cx);
	pr_regs4(regs, ax, r8, r9, r10);
	pr_regs4(regs, r11, r12, r13, r14);
	pr_regs3(regs, r15, bp, bx);
	pr_regs4(regs, orig_ax, flags, fs_base, gs_base);
	pr_msg("\n");
}

void show_thread_info(ThreadInfoX86 *thread_info)
{
	if (!thread_info)
		return;

	pr_msg("\t---[ Thread info ]---\n");
	pr_msg("\tclear_tid_addr:  0x%"PRIx64"\n", thread_info->clear_tid_addr);
	pr_msg("\n");

	show_core_regs(thread_info->gpregs);
}

static struct {
	u32 magic;
	u32 mask;
	char *hint;
} magic_hints[] = {
	{ .magic = 0x45311224, .mask = 0xffffffff, .hint = "ip route dump", },
	{ .magic = 0x47361222, .mask = 0xffffffff, .hint = "ip ifaddr dump", },
	{ .magic = 0x00008b1f, .mask = 0x0000ffff, .hint = "gzip file", },
	{ },
};

static void try_hint_magic(u32 magic)
{
	int i;

	for (i = 0; magic_hints[i].hint != 0; i++)
		if ((magic & magic_hints[i].mask) == magic_hints[i].magic)
			pr_msg("This can be %s\n", magic_hints[i].hint);
}

#define SHOW_PLAIN(name) { name##_MAGIC, PB_##name, false, NULL, NULL, }
/* nothing special behind this -S, just to avoid heavy patching */
#define SHOW_PLAINS(name) { name##S_MAGIC, PB_##name, false, NULL, NULL, }
#define SHOW_VERT(name) { name##_MAGIC, PB_##name, true, NULL, NULL, }

static struct show_image_info show_infos[] = {
	SHOW_VERT(INVENTORY),
	SHOW_VERT(CORE),
	SHOW_VERT(IDS),
	SHOW_VERT(CREDS),
	SHOW_VERT(UTSNS),
	SHOW_VERT(IPC_VAR),
	SHOW_VERT(FS),
	SHOW_VERT(GHOST_FILE),
	SHOW_VERT(MM),

	SHOW_PLAINS(REG_FILE),
	SHOW_PLAINS(NS_FILE),
	SHOW_PLAIN(EVENTFD_FILE),
	SHOW_PLAIN(EVENTPOLL_FILE),
	SHOW_PLAIN(EVENTPOLL_TFD),
	SHOW_PLAIN(SIGNALFD),
	SHOW_PLAIN(INOTIFY_FILE),
	SHOW_PLAIN(INOTIFY_WD),
	SHOW_PLAIN(FANOTIFY_FILE),
	SHOW_PLAIN(FANOTIFY_MARK),
	SHOW_PLAINS(VMA),
	SHOW_PLAINS(PIPE),
	SHOW_PLAIN(FIFO),
	SHOW_PLAIN(SIGACT),
	SHOW_PLAIN(NETLINK_SK),
	SHOW_PLAIN(REMAP_FPATH),
	SHOW_PLAINS(MNT),
	SHOW_PLAINS(TTY_FILE),
	SHOW_PLAIN(TTY_INFO),
	SHOW_PLAINS(FILE_LOCK),
	SHOW_PLAIN(RLIMIT),
	SHOW_PLAIN(TUNFILE),

	{ TCP_STREAM_MAGIC,	PB_TCP_STREAM,		true,	show_tcp_stream, "1:%u 2:%u 3:%u 4:%u", },
	{ STATS_MAGIC,		PB_STATS,		true,	NULL, "1.1:%u 1.2:%u 1.3:%u 1.4:%u 1.5:%Lu 1.6:%Lu 1.7:%Lu", },
	{ FDINFO_MAGIC,		PB_FDINFO,		false,	NULL, "flags:%#o fd:%d", },
	{ UNIXSK_MAGIC,		PB_UNIX_SK,		false,	NULL, "1:%#x 2:%#x 3:%d 4:%d 5:%d 6:%d 7:%d 8:%#x 11:S", },
	{ INETSK_MAGIC,		PB_INET_SK,		false,	NULL, "1:%#x 2:%#x 3:%d 4:%d 5:%d 6:%d 7:%d 8:%d 9:%2x 11:A 12:A", },
	{ PACKETSK_MAGIC,	PB_PACKET_SOCK,		false,	NULL, "5:%d", },
	{ ITIMERS_MAGIC,	PB_ITIMER,		false,	NULL, "*:%Lu", },
	{ POSIX_TIMERS_MAGIC,	PB_POSIX_TIMER,		false,	NULL, "*:%d 5:%Lu 7:%Lu 8:%lu 9:%Lu 10:%Lu", },
	{ NETDEV_MAGIC,		PB_NETDEV, 		false,	NULL, "2:%d", },

	{ PAGEMAP_MAGIC,	PB_PAGEMAP_HEAD,	true,	show_pagemaps,		NULL, },
	{ PIPES_DATA_MAGIC,	PB_PIPE_DATA,		false,	pipe_data_handler,	NULL, },
	{ FIFO_DATA_MAGIC,	PB_PIPE_DATA,		false,	pipe_data_handler,	NULL, },
	{ SK_QUEUES_MAGIC,	PB_SK_QUEUES,		false,	sk_queue_data_handler,	NULL, },
	{ IPCNS_SHM_MAGIC,	PB_IPC_SHM,		false,	ipc_shm_handler,	NULL, },
	{ IPCNS_SEM_MAGIC,	PB_IPC_SEM,		false,	ipc_sem_handler,	NULL, },
	{ IPCNS_MSG_MAGIC,	PB_IPCNS_MSG_ENT,	false,	ipc_msg_handler,	NULL, },

	{ }
};

static int cr_parse_file(void)
{
	u32 magic;
	int ret = -1, fd;

	fd = open(opts.show_dump_file, O_RDONLY);
	if (fd < 0) {
		pr_perror("Can't open %s", opts.show_dump_file);
		goto out;
	}

	if (read_img(fd, &magic) < 0)
		goto out;

	ret = cr_parse_fd(fd, magic);
out:
	close_safe(&fd);
	return ret;
}

int cr_parse_fd(int fd, u32 magic)
{
	int ret = 0, i;

	if (magic == PSTREE_MAGIC) {
		show_collect_pstree(fd, 0);
		goto out;
	}

	if (magic == SIGNAL_MAGIC || magic == PSIGNAL_MAGIC) {
		show_siginfo(fd);
		goto out;
	}

	for (i = 0; show_infos[i].magic; i++) {
		struct show_image_info *si;

		si = &show_infos[i];
		if (si->magic != magic)
			continue;

		do_pb_show_plain(fd, si->pb_type, si->single,
				si->payload, si->fmt);
		goto out;
	}

	ret = -1;
	pr_err("Unknown magic %#x in %s\n",
			magic, opts.show_dump_file);
	try_hint_magic(magic);

out:
	return ret;
}

static int cr_show_pstree_item(struct pstree_item *item)
{
	int ret = -1, i;
	struct cr_fdset *cr_fdset = NULL;
	TaskKobjIdsEntry *ids;

	cr_fdset = cr_task_fdset_open(item->pid.virt, O_SHOW);
	if (!cr_fdset)
		goto out;

	pr_msg("Task %d:\n", item->pid.virt);
	pr_msg("----------------------------------------\n");

	cr_parse_fd(fdset_fd(cr_fdset, CR_FD_CORE), CORE_MAGIC);

	if (item->nr_threads > 1) {
		int fd_th;

		for (i = 0; i < item->nr_threads; i++) {

			if (item->threads[i].virt == item->pid.virt)
				continue;

			fd_th = open_image(CR_FD_CORE, O_SHOW, item->threads[i].virt);
			if (fd_th < 0)
				goto outc;

			pr_msg("Thread %d.%d:\n", item->pid.virt, item->threads[i].virt);
			pr_msg("----------------------------------------\n");

			cr_parse_fd(fd_th, CORE_MAGIC);
			close_safe(&fd_th);
		}
	}

	pr_msg("Resources for %d:\n", item->pid.virt);
	pr_msg("----------------------------------------\n");
	for (i = _CR_FD_TASK_FROM + 1; i < _CR_FD_TASK_TO; i++)
		if ((i != CR_FD_CORE) && (i != CR_FD_IDS)) {
			pr_msg("* ");
			pr_msg(fdset_template[i].fmt, item->pid.virt);
			pr_msg(":\n");
			cr_parse_fd(fdset_fd(cr_fdset, i), fdset_template[i].magic);
		}

	if (pb_read_one(fdset_fd(cr_fdset, CR_FD_IDS), &ids, PB_IDS) > 0) {
		i = open_image(CR_FD_FDINFO, O_SHOW, ids->files_id);
		if (i >= 0) {
			pr_msg("* ");
			pr_msg(fdset_template[CR_FD_FDINFO].fmt, ids->files_id);
			pr_msg(":\n");

			cr_parse_fd(i, FDINFO_MAGIC);
			close(i);
		}

		task_kobj_ids_entry__free_unpacked(ids, NULL);
	}

	pr_msg("---[ end of task %d ]---\n", item->pid.virt);

	ret = 0;
outc:
	close_cr_fdset(&cr_fdset);
out:
	return ret;
}

static int cr_show_pid(int pid)
{
	int fd, ret;
	struct pstree_item item;

	fd = open_image(CR_FD_PSTREE, O_SHOW);
	if (fd < 0)
		return -1;

	while (1) {
		PstreeEntry *pe;

		ret = pb_read_one_eof(fd, &pe, PB_PSTREE);
		if (ret <= 0){
			close(fd);
			return ret;
		}

		if (pe->pid == pid) {
			pstree_item_from_pb(pe, &item);
			pstree_entry__free_unpacked(pe, NULL);
			break;
		}

		pstree_entry__free_unpacked(pe, NULL);
	}

	close(fd);

	return cr_show_pstree_item(&item);
}

static int cr_show_all(void)
{
	struct pstree_item *item = NULL, *tmp;
	int ret = -1, fd, pid;

	fd = open_image(CR_FD_PSTREE, O_SHOW);
	if (fd < 0)
		goto out;
	show_collect_pstree(fd, 1);
	close(fd);

	fd = open_image(CR_FD_SK_QUEUES, O_SHOW);
	if (fd < 0)
		goto out;

	close(fd);

	pid = list_first_entry(&pstree_list, struct pstree_item, sibling)->pid.virt;
	ret = try_show_namespaces(pid);
	if (ret)
		goto out;

	list_for_each_entry(item, &pstree_list, sibling)
		if (cr_show_pstree_item(item))
			break;

out:
	list_for_each_entry_safe(item, tmp, &pstree_list, sibling) {
		list_del(&item->sibling);
		xfree(item->threads);
		xfree(item);
	}
	return ret;
}

int cr_show(int pid)
{
	if (opts.show_dump_file)
		return cr_parse_file();

	if (pid)
		return cr_show_pid(pid);

	return cr_show_all();
}
