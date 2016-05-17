/*
   Copyright 2005-2010 Jakub Kruszona-Zawadzki, Gemius SA, 2013-2014 EditShare, 2013-2016
   Skytechnology sp. z o.o..

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

#pragma once

#include "common/platform.h"

#include "protocol/chunkserver_list_entry.h"

class matocsserventry;

/*! \brief Structure keeping extra information for chunkserver. */
struct csdbentry {
	matocsserventry *eptr; /*!< Handle to chunkserver. */

	csdbentry() : eptr() {
	}
	explicit csdbentry(matocsserventry *eptr) : eptr(eptr) {
	}
};

/*! \brief Register new connection to chunkserver.
 *
 * \param ip Chunkserver ip.
 * \param port Chunkserver port.
 * \param eptr Pointer to chunkserver handle.
 *
 * \return -1 if chunkserver was already registered.
 *          0 if chunkserver reconnected (is registered in database but with null handle)
 *          1 if chunkserver was succesfuly registered.
 */
int csdb_new_connection(uint32_t ip, uint16_t port, matocsserventry *eptr);

/*! \brief Mark that connection to chunkserver is lost.
 *
 * \param ip Chunkserver ip.
 * \param port Chunkserver port.
 */
void csdb_lost_connection(uint32_t ip, uint16_t port);

/*! \brief Get information about all chunkservers.
 *
 * This list includes disconnected chunkservers.
 * Disconnected chunkservers have the following fields set to non-zero:
 * \p version (set to \p kDisconnectedChunkserverVersion), \p servip, \p servport.
 */
std::vector<ChunkserverListEntry> csdb_chunkserver_list();

/*! \brief Unregister chunkserver.
 *
 * \param ip Chunkserver ip.
 * \param port Chunkserver port.
 *
 * \return -1 unregistered chunkserver was connected.
 *          0 chunkserver with matching ip + port was not found.
 *          1 chunkserver was disconnected before removal.
 */
int csdb_remove_server(uint32_t ip, uint16_t port);
