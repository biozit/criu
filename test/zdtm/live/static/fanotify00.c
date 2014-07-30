#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/fanotify.h>

#include "zdtmtst.h"

#ifdef __x86_64__
# define __NR_fanotify_init	300
# define __NR_fanotify_mark	301
#else
# define __NR_fanotify_init	338
# define __NR_fanotify_mark	339
#endif

const char *test_doc	= "Check for fanotify delivery";
const char *test_author	= "Cyrill Gorcunov <gorcunov@openvz.org>";

const char fanotify_path[] = "fanotify-del-after-cr";

#define BUFF_SIZE (8192)

struct fanotify_mark_inode {
	unsigned long		i_ino;
	unsigned int		s_dev;
	unsigned int		mflags;
	unsigned int		mask;
	unsigned int		ignored_mask;
	unsigned int		fhandle_bytes;
	unsigned int		fhandle_type;
	unsigned char		fhandle[512];
};

struct fanotify_mark_mount {
	unsigned int		mnt_id;
	unsigned int		mflags;
	unsigned int		mask;
	unsigned int		ignored_mask;
};

struct fanotify_glob {
	unsigned int		faflags;
	unsigned int		evflags;
};

struct fanotify_obj {
	struct fanotify_glob		glob;
	struct fanotify_mark_inode	inode;
	struct fanotify_mark_mount	mount;
};

static int fanotify_init(unsigned int flags, unsigned int event_f_flags)
{
	return syscall(__NR_fanotify_init, flags, event_f_flags);
}

static int fanotify_mark(int fanotify_fd, unsigned int flags, unsigned long mask,
			 int dfd, const char *pathname)
{
	return syscall(__NR_fanotify_mark, fanotify_fd, flags, mask, dfd, pathname);
}

#define fdinfo_field(str, field)	!strncmp(str, field":", sizeof(field))

static void show_fanotify_obj(struct fanotify_obj *obj)
{
	test_msg("fanotify obj at %p\n", obj);

	test_msg(" glob\n");
	test_msg("  faflags: %x evflags: %x\n",
		 obj->glob.faflags, obj->glob.evflags);

	test_msg(" inode\n");
	test_msg("  i_ino: %lx s_dev: %x mflags: %x "
		 "mask: %x ignored_mask: %x "
		 "fhandle_bytes: %x fhandle_type: %x"
		 "fhandle: %s",
		 obj->inode.i_ino, obj->inode.s_dev,
		 obj->inode.mflags, obj->inode.mask,
		 obj->inode.ignored_mask, obj->inode.fhandle_bytes,
		 obj->inode.fhandle_type, obj->inode.fhandle);

	test_msg(" mount\n");
	test_msg("  mnt_id: %x mflags: %x mask: %x ignored_mask: %x\n",
		 obj->mount.mnt_id, obj->mount.mflags,
		 obj->mount.mask, obj->mount.ignored_mask);
}

static void copy_fhandle(char *tok, struct fanotify_mark_inode *inode)
{
	int off = 0;

	while (*tok && (*tok > '0' || *tok < 'f')) {
		inode->fhandle[off++] = *tok++;
		if (off >= sizeof(inode->fhandle) - 1)
			break;
	}
	inode->fhandle[off] = '\0';
}

int parse_fanotify_fdinfo(int fd, struct fanotify_obj *obj, unsigned int expected_to_meet)
{
	unsigned int met = 0;
	char str[512];
	FILE *f;
	int ret;

	sprintf(str, "/proc/self/fdinfo/%d", fd);
	f = fopen(str, "r");
	if (!f) {
		err("Can't open fdinfo to parse");
		return -1;
	}

	while (fgets(str, sizeof(str), f)) {
		if (fdinfo_field(str, "fanotify flags")) {
			ret = sscanf(str, "fanotify flags:%x event-flags:%x",
				     &obj->glob.faflags, &obj->glob.evflags);
			if (ret != 2)
				goto parse_err;
			met++;
			continue;
		}
		if (fdinfo_field(str, "fanotify mnt_id")) {
			ret = sscanf(str,
				     "fanotify mnt_id:%x mflags:%x mask:%x ignored_mask:%x",
				     &obj->mount.mnt_id, &obj->mount.mflags,
				     &obj->mount.mask, &obj->mount.ignored_mask);
			if (ret != 4)
				goto parse_err;
			met++;
			continue;
		}
		if (fdinfo_field(str, "fanotify ino")) {
			int hoff;
			ret = sscanf(str,
				     "fanotify ino:%lx sdev:%x mflags:%x mask:%x ignored_mask:%x "
				     "fhandle-bytes:%x fhandle-type:%x f_handle: %n",
				     &obj->inode.i_ino, &obj->inode.s_dev,
				     &obj->inode.mflags, &obj->inode.mask, &obj->inode.ignored_mask,
				     &obj->inode.fhandle_bytes, &obj->inode.fhandle_type,
				     &hoff);
			if (ret != 7)
				goto parse_err;
			copy_fhandle(&str[hoff], &obj->inode);
			met++;
			continue;
		}
	}

	if (expected_to_meet != met) {
		err("Expected to meet %d entries but got %d\n",
		    expected_to_meet, met);
		return -1;
	}

	return 0;

parse_err:
	err("Can't parse '%s'\n", str);
	return -1;
}

int main (int argc, char *argv[])
{
	struct fanotify_obj old = { }, new = { };
	int fa_fd, fd, del_after;
	char buf[BUFF_SIZE];
	ssize_t length;


	test_init(argc, argv);

	fa_fd = fanotify_init(FAN_NONBLOCK | O_RDONLY | O_LARGEFILE |
			      FAN_CLASS_NOTIF | FAN_UNLIMITED_QUEUE,
			      0);
	if (fa_fd < 0) {
		err("fanotify_init failed\n");
		exit(1);
	}

	del_after = open(fanotify_path, O_CREAT | O_TRUNC);
	if (del_after < 0) {
		err("open failed\n");
		exit(1);
	}

	if (fanotify_mark(fa_fd, FAN_MARK_ADD,
			  FAN_MODIFY | FAN_ACCESS | FAN_OPEN | FAN_CLOSE,
			  AT_FDCWD, fanotify_path)) {
		err("fanotify_mark failed\n");
		exit(1);
	}

	if (fanotify_mark(fa_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
			  FAN_ONDIR | FAN_OPEN | FAN_CLOSE,
			  AT_FDCWD, "/")) {
		err("fanotify_mark failed\n");
		exit(1);
	}

	if (fanotify_mark(fa_fd, FAN_MARK_ADD | FAN_MARK_MOUNT |
			  FAN_MARK_IGNORED_MASK | FAN_MARK_IGNORED_SURV_MODIFY,
			  FAN_MODIFY | FAN_ACCESS,
			  AT_FDCWD, "/")) {
		err("fanotify_mark failed\n");
		exit(1);
	}

	if (parse_fanotify_fdinfo(fa_fd, &old, 3)) {
		err("parsing fanotify fdinfo failed\n");
		exit(1);
	}

	show_fanotify_obj(&old);

	test_daemon();
	test_waitsig();

	fd = open("/", O_RDONLY);
	close(fd);

	fd = open(fanotify_path, O_RDWR);
	close(fd);

	if (unlink(fanotify_path)) {
		fail("can't unlink %s\n", fanotify_path);
		exit(1);
	}

	if (parse_fanotify_fdinfo(fa_fd, &new, 3)) {
		fail("parsing fanotify fdinfo failed\n");
		exit(1);
	}

	show_fanotify_obj(&new);

	if (memcmp(&old, &new, sizeof(old))) {
		fail("fanotify mismatch on fdinfo level\n");
		exit(1);
	}

	length = read(fa_fd, buf, sizeof(buf));
	if (length <= 0) {
		fail("No events in fanotify queue\n");
		exit(1);
	}

	pass();

	return 0;
}
