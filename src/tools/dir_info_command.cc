/*
   Copyright 2005-2010 Jakub Kruszona-Zawadzki, Gemius SA, 2013-2014 EditShare,
   2013-2016 Skytechnology sp. z o.o..

   This file was part of MooseFS and is part of LizardFS.

   LizardFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   LizardFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LizardFS. If not, see <http://www.gnu.org/licenses/>.
 */

#include "common/platform.h"

#include <stdio.h>
#include <stdlib.h>

#include "tools/tools_commands.h"

int dir_info(const char *fname) {
	uint8_t reqbuff[16], *wptr, *buff;
	const uint8_t *rptr;
	uint32_t cmd, leng, inode;
	uint32_t inodes, dirs, files, chunks;
	uint64_t length, size, realsize;
	int fd;
	fd = open_master_conn(fname, &inode, nullptr, 0, 0);
	if (fd < 0) {
		return -1;
	}
	wptr = reqbuff;
	put32bit(&wptr, CLTOMA_FUSE_GETDIRSTATS);
	put32bit(&wptr, 8);
	put32bit(&wptr, 0);
	put32bit(&wptr, inode);
	if (tcpwrite(fd, reqbuff, 16) != 16) {
		printf("%s: master query: send error\n", fname);
		close_master_conn(1);
		return -1;
	}
	if (tcpread(fd, reqbuff, 8) != 8) {
		printf("%s: master query: receive error\n", fname);
		close_master_conn(1);
		return -1;
	}
	rptr = reqbuff;
	cmd = get32bit(&rptr);
	leng = get32bit(&rptr);
	if (cmd != MATOCL_FUSE_GETDIRSTATS) {
		printf("%s: master query: wrong answer (type)\n", fname);
		close_master_conn(1);
		return -1;
	}
	buff = (uint8_t *)malloc(leng);
	if (tcpread(fd, buff, leng) != (int32_t)leng) {
		printf("%s: master query: receive error\n", fname);
		free(buff);
		close_master_conn(1);
		return -1;
	}
	rptr = buff;
	cmd = get32bit(&rptr);  // queryid
	if (cmd != 0) {
		printf("%s: master query: wrong answer (queryid)\n", fname);
		free(buff);
		close_master_conn(1);
		return -1;
	}
	leng -= 4;
	if (leng == 1) {
		printf("%s: %s\n", fname, mfsstrerr(*rptr));
		free(buff);
		close_master_conn(1);
		return -1;
	} else if (leng != 56 && leng != 40) {
		printf("%s: master query: wrong answer (leng)\n", fname);
		free(buff);
		close_master_conn(1);
		return -1;
	}
	close_master_conn(0);
	inodes = get32bit(&rptr);
	dirs = get32bit(&rptr);
	files = get32bit(&rptr);
	if (leng == 56) {
		rptr += 8;
	}
	chunks = get32bit(&rptr);
	if (leng == 56) {
		rptr += 8;
	}
	length = get64bit(&rptr);
	size = get64bit(&rptr);
	realsize = get64bit(&rptr);
	free(buff);
	printf("%s:\n", fname);
	print_number(" inodes:       ", "\n", inodes, 0, 0, 1);
	print_number("  directories: ", "\n", dirs, 0, 0, 1);
	print_number("  files:       ", "\n", files, 0, 0, 1);
	print_number(" chunks:       ", "\n", chunks, 0, 0, 1);
	print_number(" length:       ", "\n", length, 0, 1, 1);
	print_number(" size:         ", "\n", size, 0, 1, 1);
	print_number(" realsize:     ", "\n", realsize, 0, 1, 1);
	return 0;
}
