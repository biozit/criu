#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "zdtmtst.h"

const char *test_doc	= "Check that special charecters in paths are handled correctly";
const char *test_author	= "Andrew Vagin <avagin@virtuozzo.com>";

char *dirname;
TEST_OPTION(dirname, string, "directory name", 1);

#define TEST_DIR "tmpfs  \t \t\\\\\t test  \t\t\\\\ \t\\"

int main(int argc, char **argv)
{
	int ret = 1;
	char buf[1024], test_dir[PATH_MAX], fname[PATH_MAX];

	test_init(argc, argv);

	mkdir(dirname, 0700);

	snprintf(test_dir, sizeof(test_dir), "%s/%s", dirname, TEST_DIR);
	mkdir(test_dir, 0700);

	if (mount("", test_dir, "tmpfs", 0, NULL)) {
		err("mount");
		return 1;
	}

	snprintf(fname, sizeof(buf), "%s/\\\t \\\\ \\tt", test_dir);
	if (mkdir(fname, 0700)) {
		err("mkdir");
		return 1;
	}

	test_daemon();
	test_waitsig();

	if (access(fname, F_OK)) {
		fail();
		goto err;
	}

	pass();
	ret = 0;
err:
	umount2(test_dir, MNT_DETACH);
	rmdir(test_dir);
	rmdir(dirname);
	return ret;
}
