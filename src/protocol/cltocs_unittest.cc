/*
   Copyright 2013-2015 Skytechnology sp. z o.o.

   This file is part of LizardFS.

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
#include "protocol/cltocs.h"

#include <gtest/gtest.h>

#include "common/lizardfs_version.h"
#include "unittests/chunk_type_constants.h"
#include "unittests/inout_pair.h"
#include "unittests/operators.h"
#include "unittests/packet.h"

TEST(CltocsCommunicationTests, Read) {
	LIZARDFS_DEFINE_INOUT_PAIR(uint64_t, chunkId, 0x0123456789ABCDEF, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, chunkVersion, 0x01234567, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(ChunkPartType, chunkType, xor_p_of_7, standard);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, readOffset, 2 * MFSBLOCKSIZE, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, readSize, 5 * MFSBLOCKSIZE, 0);

	std::vector<uint8_t> buffer;
	ASSERT_NO_THROW(cltocs::read::serialize(buffer,
			chunkIdIn, chunkVersionIn, chunkTypeIn, readOffsetIn, readSizeIn));

	verifyHeader(buffer, LIZ_CLTOCS_READ);
	removeHeaderInPlace(buffer);
	ASSERT_NO_THROW(cltocs::read::deserialize(buffer.data(), buffer.size(),
			chunkIdOut, chunkVersionOut, chunkTypeOut, readOffsetOut, readSizeOut));

	LIZARDFS_VERIFY_INOUT_PAIR(chunkId);
	LIZARDFS_VERIFY_INOUT_PAIR(chunkVersion);
	LIZARDFS_VERIFY_INOUT_PAIR(chunkType);
	LIZARDFS_VERIFY_INOUT_PAIR(readOffset);
	LIZARDFS_VERIFY_INOUT_PAIR(readSize);
}

TEST(CltocsCommunicationTests, WriteInit) {
	LIZARDFS_DEFINE_INOUT_PAIR(uint64_t, chunkId,  0x987654321, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, chunkVersion, 0x01234567, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(ChunkPartType, chunkType, xor_p_of_7, standard);
	LIZARDFS_DEFINE_INOUT_VECTOR_PAIR(ChunkTypeWithAddress, chain) = {
			ChunkTypeWithAddress(NetworkAddress(0x0A000001, 12388), slice_traits::standard::ChunkPartType(), kStdVersion),
			ChunkTypeWithAddress(NetworkAddress(0x0A000002, 12389), slice_traits::standard::ChunkPartType(), kStdVersion),
	};

	std::vector<uint8_t> buffer;
	ASSERT_NO_THROW(cltocs::writeInit::serialize(buffer,
			chunkIdIn, chunkVersionIn, chunkTypeIn, chainIn));

	verifyHeader(buffer, LIZ_CLTOCS_WRITE_INIT);
	removeHeaderInPlace(buffer);
	ASSERT_NO_THROW(cltocs::writeInit::deserialize(buffer.data(), buffer.size(),
			chunkIdOut, chunkVersionOut, chunkTypeOut, chainOut));

	LIZARDFS_VERIFY_INOUT_PAIR(chunkId);
	LIZARDFS_VERIFY_INOUT_PAIR(chunkVersion);
	LIZARDFS_VERIFY_INOUT_PAIR(chunkType);
	LIZARDFS_VERIFY_INOUT_PAIR(chain);
}

TEST(CltocsCommunicationTests, WriteData) {
	LIZARDFS_DEFINE_INOUT_PAIR(uint64_t, chunkId,  0x987654321, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, writeId,  0x12345,     0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint16_t, blockNum, 510,         0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, offset,   1024,        0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, size,     62000,       0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, crc,      0xDEADBEEF,  0);

	std::vector<uint8_t> buffer;
	ASSERT_NO_THROW(cltocs::writeData::serializePrefix(buffer,
			chunkIdIn, writeIdIn, blockNumIn, offsetIn, sizeIn, crcIn));
	EXPECT_EQ(buffer.size() - PacketHeader::kSize, cltocs::writeData::kPrefixSize);

	verifyHeaderInPrefix(buffer, LIZ_CLTOCS_WRITE_DATA, sizeIn);
	removeHeaderInPlace(buffer);
	ASSERT_NO_THROW(cltocs::writeData::deserializePrefix(buffer.data(), buffer.size(),
			chunkIdOut, writeIdOut, blockNumOut, offsetOut, sizeOut, crcOut));

	LIZARDFS_VERIFY_INOUT_PAIR(chunkId);
	LIZARDFS_VERIFY_INOUT_PAIR(writeId);
	LIZARDFS_VERIFY_INOUT_PAIR(blockNum);
	LIZARDFS_VERIFY_INOUT_PAIR(offset);
	LIZARDFS_VERIFY_INOUT_PAIR(size);
	LIZARDFS_VERIFY_INOUT_PAIR(crc);
}

TEST(CltocsCommunicationTests, WriteEnd) {
	LIZARDFS_DEFINE_INOUT_PAIR(uint64_t, chunkId, 0x987654321, 0);

	std::vector<uint8_t> buffer;
	ASSERT_NO_THROW(cltocs::writeEnd::serialize(buffer, chunkIdIn));

	verifyHeader(buffer, LIZ_CLTOCS_WRITE_END);
	removeHeaderInPlace(buffer);
	ASSERT_NO_THROW(cltocs::writeEnd::deserialize(buffer.data(), buffer.size(), chunkIdOut));

	LIZARDFS_VERIFY_INOUT_PAIR(chunkId);
}

TEST(CltocsCommunicationTests, TestChunk) {
	LIZARDFS_DEFINE_INOUT_PAIR(uint64_t, chunkId,  0x987654321, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(uint32_t, chunkVersion, 0x01234567, 0);
	LIZARDFS_DEFINE_INOUT_PAIR(ChunkPartType, chunkType, xor_p_of_7, standard);

	std::vector<uint8_t> buffer;
	ASSERT_NO_THROW(cltocs::testChunk::serialize(buffer,
			chunkIdIn, chunkVersionIn, chunkTypeIn));

	verifyHeader(buffer, LIZ_CLTOCS_TEST_CHUNK);
	removeHeaderInPlace(buffer);
	ASSERT_NO_THROW(cltocs::testChunk::deserialize(buffer.data(), buffer.size(),
			chunkIdOut, chunkVersionOut, chunkTypeOut));

	LIZARDFS_VERIFY_INOUT_PAIR(chunkId);
	LIZARDFS_VERIFY_INOUT_PAIR(chunkVersion);
	LIZARDFS_VERIFY_INOUT_PAIR(chunkType);
}
