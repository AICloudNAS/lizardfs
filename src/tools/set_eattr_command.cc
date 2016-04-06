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

int set_eattr(const char *fname, uint8_t eattr, uint8_t mode) {
	uint8_t reqbuff[22], *wptr, *buff;
	const uint8_t *rptr;
	uint32_t cmd, leng, inode, uid;
	uint32_t changed, notchanged, notpermitted;
	int fd;
	fd = open_master_conn(fname, &inode, nullptr, 0, 1);
	if (fd < 0) {
		return -1;
	}
	uid = getuid();
	wptr = reqbuff;
	put32bit(&wptr, CLTOMA_FUSE_SETEATTR);
	put32bit(&wptr, 14);
	put32bit(&wptr, 0);
	put32bit(&wptr, inode);
	put32bit(&wptr, uid);
	put8bit(&wptr, eattr);
	put8bit(&wptr, mode);
	if (tcpwrite(fd, reqbuff, 22) != 22) {
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
	if (cmd != MATOCL_FUSE_SETEATTR) {
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
	close_master_conn(0);
	rptr = buff;
	cmd = get32bit(&rptr);  // queryid
	if (cmd != 0) {
		printf("%s: master query: wrong answer (queryid)\n", fname);
		free(buff);
		return -1;
	}
	leng -= 4;
	if (leng == 1) {
		printf("%s: %s\n", fname, mfsstrerr(*rptr));
		free(buff);
		return -1;
	} else if (leng != 12) {
		printf("%s: master query: wrong answer (leng)\n", fname);
		free(buff);
		return -1;
	}
	changed = get32bit(&rptr);
	notchanged = get32bit(&rptr);
	notpermitted = get32bit(&rptr);
	if ((mode & SMODE_RMASK) == 0) {
		if (changed) {
			printf("%s: attribute(s) changed\n", fname);
		} else {
			printf("%s: attribute(s) not changed\n", fname);
		}
	} else {
		printf("%s:\n", fname);
		print_number(" inodes with attributes changed:     ", "\n", changed, 1, 0, 1);
		print_number(" inodes with attributes not changed: ", "\n", notchanged, 1, 0, 1);
		print_number(" inodes with permission denied:      ", "\n", notpermitted, 1, 0, 1);
	}
	free(buff);
	return 0;
}
