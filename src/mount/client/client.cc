/*
   Copyright 2017 Skytechnology sp. z o.o..

   This file is part of LizardFS.

   LizardFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   LizardFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LizardFS  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common/platform.h"

#include "client.h"

#include <dlfcn.h>
#include <fstream>

#include "client_error_code.h"

#define LIZARDFS_LINK_FUNCTION(function_name) do { \
	void *function_name##_ptr = dlsym(dl_handle_, #function_name); \
	function_name##_ = *(decltype(function_name##_)*)&function_name##_ptr; \
	if (function_name##_ == nullptr) { \
		throw std::runtime_error(std::string("dl lookup failed for ") + #function_name); \
	} \
} while (0)

using namespace lizardfs;

std::atomic<int> Client::instance_count_(0);

void *Client::linkLibrary() {
	void *ret;

	// Special case for the first instance - no copying needed
	if (instance_count_++ == 0) {
		ret = dlopen(kLibraryPath, RTLD_NOW);
		if (ret == nullptr) {
			instance_count_--;
			throw std::runtime_error(std::string("Cannot link: ") + dlerror());
		}
		return ret;
	}

	char pattern[] = "/tmp/liblizardfsmount_shared.so.XXXXXX";
	int out_fd = ::mkstemp(pattern);
	if (out_fd < 0) {
		instance_count_--;
		throw std::runtime_error("Cannot create temporary file");
	}

	std::ifstream source(kLibraryPath);
	std::ofstream dest(pattern);

	dest << source.rdbuf();

	source.close();
	dest.close();
	ret = dlopen(pattern, RTLD_NOW);
	::close(out_fd);
	::unlink(pattern);
	if (ret == nullptr) {
		instance_count_--;
		throw std::runtime_error(std::string("Cannot link: ") + dlerror());
	}
	return ret;
}

Client::Client(const std::string &host, const std::string &port, const std::string &mountpoint)
		: fileinfos_() {
	dl_handle_ = linkLibrary();
	try {
		LIZARDFS_LINK_FUNCTION(lzfs_disable_printf);
		LIZARDFS_LINK_FUNCTION(lizardfs_fs_init);
		LIZARDFS_LINK_FUNCTION(lizardfs_fs_term);
		LIZARDFS_LINK_FUNCTION(lizardfs_lookup);
		LIZARDFS_LINK_FUNCTION(lizardfs_mknod);
		LIZARDFS_LINK_FUNCTION(lizardfs_open);
		LIZARDFS_LINK_FUNCTION(lizardfs_getattr);
		LIZARDFS_LINK_FUNCTION(lizardfs_read);
		LIZARDFS_LINK_FUNCTION(lizardfs_read_special_inode);
		LIZARDFS_LINK_FUNCTION(lizardfs_write);
		LIZARDFS_LINK_FUNCTION(lizardfs_release);
		LIZARDFS_LINK_FUNCTION(lizardfs_flush);
		LIZARDFS_LINK_FUNCTION(lizardfs_isSpecialInode);
		LIZARDFS_LINK_FUNCTION(lizardfs_update_groups);
		LIZARDFS_LINK_FUNCTION(lizardfs_readdir);
		LIZARDFS_LINK_FUNCTION(lizardfs_opendir);
		LIZARDFS_LINK_FUNCTION(lizardfs_releasedir);
		LIZARDFS_LINK_FUNCTION(lizardfs_rmdir);
		LIZARDFS_LINK_FUNCTION(lizardfs_mkdir);
		LIZARDFS_LINK_FUNCTION(lizardfs_makesnapshot);
	} catch (const std::runtime_error &e) {
		dlclose(dl_handle_);
		instance_count_--;
		throw e;
	}

	lzfs_disable_printf_();
	if (init(host, port, mountpoint) != 0) {
		assert(dl_handle_);
		dlclose(dl_handle_);
		instance_count_--;
		throw std::runtime_error("Can't connect to master server");
	}
}

Client::~Client() {
	assert(instance_count_ >= 1);
	assert(dl_handle_);

	Context ctx(0, 0, 0, 0);
	while (!fileinfos_.empty()) {
		release(ctx, std::addressof(fileinfos_.front()));
	}

	lizardfs_fs_term_();
	dlclose(dl_handle_);
	instance_count_--;
}

int Client::init(const std::string &host, const std::string &port, const std::string &mountpoint) {
	LizardClient::FsInitParams params("", host, port, mountpoint);
	return lizardfs_fs_init_(params);
}

void Client::updateGroups(Context &ctx) {
	std::error_code ec;
	updateGroups(ctx, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::updateGroups(Context &ctx, std::error_code &ec) {
	auto ret = lizardfs_update_groups_(ctx);
	ec = make_error_code(ret);
}


void Client::lookup(const Context &ctx, Inode parent, const std::string &path, EntryParam &param) {
	std::error_code ec;
	lookup(ctx, parent, path, param, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::lookup(const Context &ctx, Inode parent, const std::string &path, EntryParam &param,
		std::error_code &ec) {
	int ret = lizardfs_lookup_(ctx, parent, path.c_str(), param);
	ec = make_error_code(ret);
}

void Client::mknod(const Context &ctx, Inode parent, const std::string &path, mode_t mode,
		EntryParam &param) {
	std::error_code ec;
	mknod(ctx, parent, path, mode, param, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::mknod(const Context &ctx, Inode parent, const std::string &path, mode_t mode,
		EntryParam &param, std::error_code &ec) {
	int ret = lizardfs_mknod_(ctx, parent, path.c_str(), mode, 0, param);
	ec = make_error_code(ret);
}

Client::ReadDirReply Client::readdir(const Context &ctx, FileInfo* fileinfo, off_t offset,
		size_t max_entries) {
	std::error_code ec;
	auto dir_entries = readdir(ctx, fileinfo, offset, max_entries, ec);
	if (ec) {
		throw std::system_error(ec);
	}
	return dir_entries;
}

Client::ReadDirReply Client::readdir(const Context &ctx, FileInfo* fileinfo, off_t offset,
		size_t max_entries, std::error_code &ec) {
	auto ret = lizardfs_readdir_(ctx, fileinfo->inode, offset, max_entries);
	ec = make_error_code(ret.first);
	return ret.second;
}

Client::FileInfo *Client::opendir(const Context &ctx, Inode inode) {
	std::error_code ec;
	auto fileinfo = opendir(ctx, inode, ec);
	if (ec) {
		assert(!fileinfo);
		throw std::system_error(ec);
	}
	return fileinfo;
}

Client::FileInfo *Client::opendir(const Context &ctx, Inode inode, std::error_code &ec) {
	int ret = lizardfs_opendir_(ctx, inode);
	ec = make_error_code(ret);
	if (ec) {
		return nullptr;
	}
	FileInfo *fileinfo = new FileInfo(inode);
	std::lock_guard<std::mutex> guard(mutex_);
	fileinfos_.push_front(*fileinfo);
	return fileinfo;
}

void Client::releasedir(const Context &ctx, FileInfo* fileinfo) {
	std::error_code ec;
	releasedir(ctx, fileinfo, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::releasedir(const Context &ctx, FileInfo* fileinfo, std::error_code &ec) {
	assert(fileinfo != nullptr);
	int ret = lizardfs_releasedir_(ctx, fileinfo->inode);
	ec = make_error_code(ret);
	{
		std::lock_guard<std::mutex> guard(mutex_);
		fileinfos_.erase(fileinfos_.iterator_to(*fileinfo));
	}
	delete fileinfo;
}

void Client::rmdir(const Context &ctx, Inode parent, const std::string &path) {
	std::error_code ec;
	rmdir(ctx, parent, path, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::rmdir(const Context &ctx, Inode parent, const std::string &path, std::error_code &ec) {
	int ret = lizardfs_rmdir_(ctx, parent, path.c_str());
	ec = make_error_code(ret);
}

void Client::mkdir(const Context &ctx, Inode parent, const std::string &path, mode_t mode,
	          Client::EntryParam &entry_param) {
	std::error_code ec;
	mkdir(ctx, parent, path, mode, entry_param, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::mkdir(const Context &ctx, Inode parent, const std::string &path, mode_t mode,
	          Client::EntryParam &entry_param, std::error_code &ec) {
	int ret = lizardfs_mkdir_(ctx, parent, path.c_str(), mode, entry_param);
	ec = make_error_code(ret);
}

Client::FileInfo *Client::open(const Context &ctx, Inode inode, int flags) {
	std::error_code ec;
	auto fileinfo = open(ctx, inode, flags, ec);
	if (ec) {
		assert(!fileinfo);
		throw std::system_error(ec);
	}
	return fileinfo;
}

Client::FileInfo *Client::open(const Context &ctx, Inode inode, int flags, std::error_code &ec) {
	FileInfo *fileinfo = new FileInfo(inode);
	fileinfo->flags = flags;

	int ret = lizardfs_open_(ctx, inode, fileinfo);
	ec = make_error_code(ret);
	if (ec) {
		delete fileinfo;
		return nullptr;
	}
	std::lock_guard<std::mutex> guard(mutex_);
	fileinfos_.push_front(*fileinfo);
	return fileinfo;
}

void Client::getattr(const Context &ctx, Inode inode, AttrReply &attr_reply) {
	std::error_code ec;
	getattr(ctx, inode, attr_reply, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::getattr(const Context &ctx, Inode inode, AttrReply &attr_reply,
		std::error_code &ec) {
	int ret = lizardfs_getattr_(ctx, inode, attr_reply);
	ec = make_error_code(ret);
}

Client::ReadResult Client::read(const Context &ctx, FileInfo *fileinfo,
	                       off_t offset, std::size_t size) {
	std::error_code ec;
	auto ret = read(ctx, fileinfo, offset, size, ec);
	if (ec) {
		throw std::system_error(ec);
	}
	return ret;
}

Client::ReadResult Client::read(const Context &ctx, FileInfo *fileinfo,
	                       off_t offset, std::size_t size, std::error_code &ec) {
	if (lizardfs_isSpecialInode_(fileinfo->inode)) {
		auto ret = lizardfs_read_special_inode_(ctx, fileinfo->inode, size, offset, fileinfo);
		ec = make_error_code(ret.first);
		if (ec) {
			return ReadResult();
		}
		return ReadResult(std::move(ret.second));
	} else {
		auto ret = lizardfs_read_(ctx, fileinfo->inode, size, offset, fileinfo);
		ec = make_error_code(ret.first);
		if (ec) {
			return ReadResult();
		}
		return std::move(ret.second);
	}
}

std::size_t Client::write(const Context &ctx, FileInfo *fileinfo, off_t offset, std::size_t size,
		const char *buffer) {
	std::error_code ec;
	auto write_size = write(ctx, fileinfo, offset, size, buffer, ec);
	if (ec) {
		throw std::system_error(ec);
	}
	return write_size;
}

std::size_t Client::write(const Context &ctx, FileInfo *fileinfo, off_t offset, std::size_t size,
		const char *buffer, std::error_code &ec) {
	std::pair<int, ssize_t> ret =
	        lizardfs_write_(ctx, fileinfo->inode, buffer, size, offset, fileinfo);
	ec = make_error_code(ret.first);
	return ec ? (std::size_t)0 : (std::size_t)ret.second;
}

void Client::release(const Context &ctx, FileInfo *fileinfo) {
	std::error_code ec;
	release(ctx, fileinfo, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::release(const Context &ctx, FileInfo *fileinfo, std::error_code &ec) {
	int ret = lizardfs_release_(ctx, fileinfo->inode, fileinfo);
	std::lock_guard<std::mutex> guard(mutex_);
	fileinfos_.erase(fileinfos_.iterator_to(*fileinfo));
	delete fileinfo;
	ec = make_error_code(ret);
}

void Client::flush(const Context &ctx, FileInfo *fileinfo) {
	std::error_code ec;
	flush(ctx, fileinfo, ec);
	if (ec) {
		throw std::system_error(ec);
	}
}

void Client::flush(const Context &ctx, FileInfo *fileinfo, std::error_code &ec) {
	int ret = lizardfs_flush_(ctx, fileinfo->inode, fileinfo);
	ec = make_error_code(ret);
}

LizardClient::JobId Client::makesnapshot(const Context &ctx, Inode src_inode, Inode dst_inode,
	                                 const std::string &dst_name, bool can_overwrite) {
	std::error_code ec;
	JobId job_id = makesnapshot(ctx, src_inode, dst_inode, dst_name, can_overwrite, ec);
	if (ec) {
		throw std::system_error(ec);
	}
	return job_id;
}

LizardClient::JobId Client::makesnapshot(const Context &ctx, Inode src_inode, Inode dst_inode,
	                                 const std::string &dst_name, bool can_overwrite,
	                                 std::error_code &ec) {
	auto ret = lizardfs_makesnapshot_(ctx, src_inode, dst_inode, dst_name, can_overwrite);
	ec = make_error_code(ret.first);
	return ret.second;
}
