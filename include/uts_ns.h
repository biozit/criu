#ifndef __CR_UTS_NS_H__
#define __CR_UTS_NS_H__

#include "crtools.h"

int dump_uts_ns(int ns_pid, struct cr_fdset *fdset);
struct cr_options;
void show_utsns(int fd, struct cr_options *);
int prepare_utsns(int pid);

extern struct ns_desc uts_ns_desc;

#endif /* __CR_UTS_NS_H__ */
