/*
 * Copyright 2017 Skytechnology sp. z o.o..
 * Author: Piotr Sarna <sarna@skytechnology.pl>
 *
 * LizardFS C API
 *
 * This library can be used to communicate with LizardFS metadata and data
 * servers from C/C++ code.
 *
 * Compile with -llizardfs-client and LizardFS C/C++ library installed.
 */

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>

#ifndef __LIZARDFS_C_API_H
#define __LIZARDFS_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t liz_inode_t;
typedef int liz_err_t;
struct liz;
typedef struct liz liz_t;
struct liz_fileinfo;
typedef struct liz_fileinfo liz_fileinfo_t;
struct liz_context;
typedef struct liz_context liz_context_t;

enum liz_special_ino {
	LIZARDFS_INODE_ERROR = 0,
	LIZARDFS_INODE_ROOT = 1,
};

/* Basic attributes of a file */
struct liz_entry {
	liz_inode_t ino;
	unsigned long generation;
	struct stat attr;
	double attr_timeout;
	double entry_timeout;
};

/* Result of setattr/getattr operations */
struct liz_attr_reply {
	struct stat attr;
	double attr_timeout;
};

/* Basic attributes of a directory */
struct liz_direntry {
	const char *name;
	struct stat attr;
	off_t next_entry_offset;
};

/* Result of getxattr, setxattr and listattr operations */
struct liz_xattr_reply {
	uint32_t value_length;
	uint8_t *value_buffer;
};

/*!
 * \brief Create a context for LizardFS operations
 *  Flavor 1: create default context with current uid/gid/pid
 *  Flavor 2: create context with custom uid/gid/pid
 *
 *  \warning Creating context with secondary groups involves calling liz_update_groups
 *  on a created context. It is the case because metadata server needs to be notified
 *  that new group set was created. If secondary groups are registered by calling
 *  liz_update_groups(ctx, instance), context is bound to instance it was registered with
 *  and should not be used with other instances.
 */
liz_context_t *liz_create_context();
liz_context_t *liz_create_user_context(uid_t uid, gid_t gid, pid_t pid, mode_t umask);

/*!
 * \brief Returns last error code set by specific calls (see below)
 */
liz_err_t liz_last_err();

/*!
 * \brief Converts native LizardFS error code to POSIX error code.
 */
int liz_error_conv(liz_err_t lizardfs_error_code);

/*!
 * \brief Destroy a context for LizardFS operations
 */
void liz_destroy_context(liz_context_t *ctx);

/*!
 * \brief Initialize a connection with master server
 * \param host master server connection host
 * \param port master server connection port
 * \param mountpoint a human-readable name for 'mountpoint' created by a connection
 * \return a LizardFS client instance, nullptr if connection is impossible,
 *  sets last error code (check with liz_last_err())
 */
liz_t *liz_init(const char *host, const char *port, const char *mountpoint);

/*!
 * \brief Update secondary group information in context
 * \param instance instance returned from liz_init
 * \param ctx context to be updated
 * \param gids array of new group ids to be set
 * \param gid_num length of gids array
 * \return 0 on success, -1 if failed, sets last error code (check with liz_last_err())
 */
int liz_update_groups(liz_t *instance, liz_context_t *ctx, gid_t *gids, int gid_num);

/*! \brief Find inode in parent directory by name
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param parent parent inode
 * \param path name to be looked up
 * \param entry structure to be filled with lookup result
 * \return 0 on success, -1 if failed, sets last error code (check with liz_last_err())
 */
int liz_lookup(liz_t *instance, liz_context_t *ctx, liz_inode_t parent, const char *path,
	       struct liz_entry *entry);

/*! \brief Create a file with given parent and name
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param parent parent inode
 * \param path name to be looked up
 * \param mode file permissions
 * \return 0 on success, -1 if failed, sets last error code (check with liz_last_err())
 */
int liz_mknod(liz_t *instance, liz_context_t *ctx, liz_inode_t parent, const char *path,
	      mode_t mode, struct liz_entry *entry);

/*! \brief Open a file by inode
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param inode inode of a file
 * \param flags open flags
 * \return fileinfo descriptor of an open file,
 *  if failed - nullptr and sets last error code (check with liz_last_err())
 */
liz_fileinfo_t *liz_open(liz_t *instance, liz_context_t *ctx, liz_inode_t inode, int flags);

/*! \brief Read bytes from open file
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param fileinfo descriptor of an open file
 * \param offset read offset
 * \param size read size
 * \param buffer buffer to be read to
 * \return number of bytes read on success,
 *  -1 if failed and sets last error code (check with liz_last_err())
 */
ssize_t liz_read(liz_t *instance, liz_context_t *ctx, liz_fileinfo_t *fileinfo, off_t offset,
	         size_t size, char *buffer);
ssize_t liz_readv(liz_t *instance, liz_context_t *ctx, liz_fileinfo_t *fileinfo, off_t offset,
	          size_t size, const struct iovec *iov, int iovcnt);

/*! \brief Write bytes to open file
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param fileinfo descriptor of an open file
 * \param offset write offset
 * \param size write size
 * \param buffer buffer to be written from
 * \return number of bytes written on success,
 *  -1 if failed and sets last error code (check with liz_last_err())
 */
ssize_t liz_write(liz_t *instance, liz_context_t *ctx, liz_fileinfo_t *fileinfo,
	          off_t offset, size_t size, const char *buffer);

/*! \brief Release a previously open file
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param fileinfo descriptor of an open file
 * \return 0 on success, -1 if failed, sets last error code (check with liz_last_err())
 */
int liz_release(liz_t *instance, liz_context_t *ctx, liz_fileinfo_t *fileinfo);

/*! \brief Flush data written to an open file
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param fileinfo descriptor of an open file
 * \return 0 on success, -1 if failed, sets last error code (check with liz_last_err())
 */
int liz_flush(liz_t *instance, liz_context_t *ctx, liz_fileinfo_t *fileinfo);

/*! \brief Get attributes by inode
 * \param instance instance returned from liz_init
 * \param ctx context returned from liz_create_context
 * \param inode inode of a file
 * \param reply structure to be filled with getattr result
 * \return 0 on success, -1 if failed, sets last error code (check with liz_last_err())
 */
int liz_getattr(liz_t *instance, liz_context_t *ctx, liz_inode_t inode,
	        struct liz_attr_reply *reply);

/*! \brief End a connection with master server
 * \param instance instance returned from liz_init
 */
void liz_destroy(liz_t *instance);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
