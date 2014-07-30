#ifndef __CR_FSNOTIFY_H__
#define __CR_FSNOTIFY_H__

#include "asm/types.h"
#include "files.h"

struct fsnotify_params {
	u32	id;
	u32	faflags;
	u32	evflags;
};

extern int is_inotify_link(int lfd);
extern int is_fanotify_link(int lfd);
extern const struct fdtype_ops inotify_dump_ops;
extern const struct fdtype_ops fanotify_dump_ops;
extern struct collect_image_info inotify_cinfo;
extern struct collect_image_info inotify_mark_cinfo;
extern struct collect_image_info fanotify_cinfo;
extern struct collect_image_info fanotify_mark_cinfo;

#endif /* __CR_FSNOTIFY_H__ */
