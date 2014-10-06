#pragma once

#include "common/platform.h"

#include <stddef.h>
#include <string.h>
#include <fuse.h>

#include "common/MFSCommunication.h"

#if defined(LIZARDFS_HAVE_MLOCKALL) && defined(RLIMIT_MEMLOCK) && defined(MCL_CURRENT) \
		&& defined(MCL_FUTURE)
#  define MFS_USE_MEMLOCK
#endif

#ifdef MFS_USE_MEMLOCK
#  include <sys/mman.h>
#endif

#if defined(__APPLE__)
#  define DEFAULT_OPTIONS "allow_other,default_permissions,daemon_timeout=600,iosize=65536"
#else
#  define DEFAULT_OPTIONS "allow_other,default_permissions"
#endif

enum {
	KEY_CFGFILE,
	KEY_META,
	KEY_HOST,
	KEY_PORT,
	KEY_BIND,
	KEY_PATH,
	KEY_PASSWORDASK,
	KEY_NOSTDMOUNTOPTIONS,
	KEY_HELP,
	KEY_VERSION,
};

#define LIZARDFS_MOUNT_DEFAULT_CHUNKSERVERREADTO 2000
#define LIZARDFS_MOUNT_DEFAULT_CHUNKSERVERWRITETO 5000
#define LIZARDFS_MOUNT_DEFAULT_RTT 200

struct mfsopts_ {
	char *masterhost;
	char *masterport;
	char *bindhost;
	char *subfolder;
	char *password;
	char *md5pass;
	unsigned nofile;
	signed nice;
#ifdef MFS_USE_MEMLOCK
	int memlock;
#endif
	int nostdmountoptions;
	int meta;
	int debug;
	int delayedinit;
	int acl;
	double aclcacheto;
	unsigned aclcachesize;
	int rwlock;
	int mkdircopysgid;
	char *sugidclearmodestr;
	SugidClearMode sugidclearmode;
	char *cachemode;
	int cachefiles;
	int keepcache;
	int passwordask;
	int donotrememberpassword;
	unsigned writecachesize;
	unsigned cachePerInodePercentage;
	unsigned writeworkers;
	unsigned ioretries;
	unsigned writewindowsize;
	double attrcacheto;
	double entrycacheto;
	double direntrycacheto;
	unsigned reportreservedperiod;
	char *iolimits;
	int chunkserverrtt;
	int chunkserverconnectreadto;
	int chunkserverbasicreadto;
	int chunkservertotalreadto;
	int prefetchxorstripes;
	int chunkserverwriteto;

	mfsopts_()
		: masterhost(NULL),
			masterport(NULL),
			bindhost(NULL),
			subfolder(NULL),
			password(NULL),
			md5pass(NULL),
			nofile(0),
			nice(-19),
#ifdef MFS_USE_MEMLOCK
			memlock(0),
#endif
			nostdmountoptions(0),
			meta(0),
			debug(0),
			delayedinit(0),
			acl(0),
			aclcacheto(1.0),
			aclcachesize(1000),
			rwlock(1),
#ifdef __linux__
			mkdircopysgid(1),
#else
			mkdircopysgid(0),
#endif
			sugidclearmodestr(NULL),
			cachemode(NULL),
			cachefiles(0),
			passwordask(0),
			donotrememberpassword(0),
			writecachesize(0),
			cachePerInodePercentage(25),
			writeworkers(10),
			ioretries(30),
			writewindowsize(15),
			attrcacheto(1.0),
			entrycacheto(0.0),
			direntrycacheto(1.0),
			reportreservedperiod(60),
			iolimits(NULL),
			chunkserverrtt(LIZARDFS_MOUNT_DEFAULT_RTT),
			chunkserverconnectreadto(LIZARDFS_MOUNT_DEFAULT_CHUNKSERVERREADTO),
			chunkserverbasicreadto(LIZARDFS_MOUNT_DEFAULT_CHUNKSERVERREADTO),
			chunkservertotalreadto(LIZARDFS_MOUNT_DEFAULT_CHUNKSERVERREADTO),
			prefetchxorstripes(0),
			chunkserverwriteto(LIZARDFS_MOUNT_DEFAULT_CHUNKSERVERWRITETO) {
	}
};

extern mfsopts_ gMountOptions;
extern int gCustomCfg;
extern char *gDefaultMountpoint;
extern fuse_opt gMfsOptsStage1[];
extern fuse_opt gMfsOptsStage2[];

void usage(const char *progname);
void mfs_opt_parse_cfg_file(const char *filename,int optional,struct fuse_args *outargs);
int mfs_opt_proc_stage1(void *data, const char *arg, int key, struct fuse_args *outargs);
int mfs_opt_proc_stage2(void *data, const char *arg, int key, struct fuse_args *outargs);