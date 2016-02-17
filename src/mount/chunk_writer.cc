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
#include "mount/chunk_writer.h"

#include <algorithm>
#include <bitset>
#include <cstring>

#include "common/block_xor.h"
#include "common/chunk_type_with_address.h"
#include "common/goal.h"
#include "common/massert.h"
#include "common/read_operation_executor.h"
#include "common/slogger.h"
#include "common/sockets.h"
#include "common/time_utils.h"
#include "devtools/request_log.h"
#include "mount/exceptions.h"
#include "mount/mastercomm.h"
#include "mount/write_executor.h"

static uint32_t gcd(uint32_t a, uint32_t b) {
	for (;;) {
		if (a == 0) {
			return b;
		}
		b %= a;
		if (b == 0) {
			return a;
		}
		a %= b;
	}
}

ChunkWriter::Operation::Operation() : unfinishedWrites(0), offsetOfEnd(0) {}

bool ChunkWriter::Operation::isExpandPossible(JournalPosition newPosition, uint32_t stripeSize) {
	// If the operation is not empty, the new JournalPosition has to be compatible with
	// the previous elements of the operation, ie. we can only expand by a new block
	// of the same stripe and the same (from, to) range
	for (const JournalPosition& position : journalPositions) {
		sassert(newPosition->chunkIndex == position->chunkIndex);
		if (newPosition->from != position->from
				|| newPosition->to != position->to
				|| (newPosition->blockIndex / stripeSize) != (position->blockIndex / stripeSize)
				|| newPosition->blockIndex == position->blockIndex) {
			return false;
		}
	}
	return true;
}

void ChunkWriter::Operation::expand(JournalPosition newPosition) {
	sassert(newPosition->type != WriteCacheBlock::kParityBlock);
	uint64_t newOffsetOfEnd = newPosition->offsetInFile() + newPosition->size();
	if (newPosition->type != WriteCacheBlock::kReadBlock && newOffsetOfEnd > offsetOfEnd) {
		offsetOfEnd = newOffsetOfEnd;
	}
	journalPositions.push_back(newPosition);
}

bool ChunkWriter::Operation::collidesWith(const Operation& operation) const {
	for (const auto& position1 : journalPositions) {
		for (const auto& position2 : operation.journalPositions) {
			sassert(position1->chunkIndex == position2->chunkIndex);
			if (position1->blockIndex != position2->blockIndex
					|| position1->from >= position2->to
					|| position1->to <= position2->from) {
				continue;
			}
			return true;
		}
	}
	return false;
}

bool ChunkWriter::Operation::isFullStripe(uint32_t stripeSize) const {
	if (journalPositions.empty()) {
		return false;
	}
	uint32_t elementsInStripe = stripeSize;
	// The last one is shorter when MFSBLOCKSINCHUNK % stripeSize != 0
	uint32_t stripe = journalPositions.front()->blockIndex / stripeSize;
	if (stripe == (MFSBLOCKSINCHUNK - 1) / stripeSize && MFSBLOCKSINCHUNK % stripeSize != 0) {
		elementsInStripe = MFSBLOCKSINCHUNK % stripeSize;
	}
	return (journalPositions.size() == elementsInStripe);
}

ChunkWriter::ChunkWriter(ChunkserverStats& chunkserverStats, ChunkConnector& connector,
		int dataChainFd)
	: chunkserverStats_(chunkserverStats),
	  connector_(connector),
	  locator_(nullptr),
	  idCounter_(0),
	  acceptsNewOperations_(true),
	  combinedStripeSize_(0),
	  dataChainFd_(dataChainFd) {
}

ChunkWriter::~ChunkWriter() {
	try {
		abortOperations();
	} catch (...) {
	}
}

void ChunkWriter::init(WriteChunkLocator* locator, uint32_t chunkserverTimeout_ms) {
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::init");
	sassert(pendingOperations_.empty());
	sassert(executors_.empty());

	Timeout connectTimeout{std::chrono::milliseconds(chunkserverTimeout_ms)};
	combinedStripeSize_ = 0;
	locator_ = locator;

	for (const ChunkTypeWithAddress& location : locator_->locationInfo().locations) {
		// If we have an executor writing the same chunkType, use it
		bool addedToChain = false;
		for (auto& fdAndExecutor : executors_) {
			if (fdAndExecutor.second->chunkType() == location.chunk_type) {
				fdAndExecutor.second->addChunkserverToChain(location);
				addedToChain = true;
			}
		}
		if (addedToChain) {
			continue;
		}

		// Update combinedStripeSize_
		uint32_t stripeSize = slice_traits::getStripeSize(location.chunk_type);
		if (combinedStripeSize_ == 0) {
			combinedStripeSize_ = stripeSize;
		} else {
			combinedStripeSize_ =
					stripeSize * combinedStripeSize_ / gcd(combinedStripeSize_, stripeSize);
		}

		// Create an executor
		int fd = connector_.startUsingConnection(location.address, connectTimeout);
		std::unique_ptr<WriteExecutor> executor(new WriteExecutor(
				chunkserverStats_, location.address, location.chunkserver_version, fd,
				chunkserverTimeout_ms, locator_->locationInfo().chunkId, locator_->locationInfo().version,
				location.chunk_type));
		executors_.insert(std::make_pair(fd, std::move(executor)));
	}

	// Initialize all the executors -- this is a special operation with id=0
	for (const auto& fdAndExecutor : executors_) {
		fdAndExecutor.second->addInitPacket();
		pendingOperations_[0].unfinishedWrites++;
	}
}

uint32_t ChunkWriter::getMinimumBlockCountWorthWriting() {
	return combinedStripeSize_;
}

uint32_t ChunkWriter::startNewOperations() {
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::startNewOperations");
	uint32_t operationsStarted = 0;
	// Start all possible operations. Break at the first operation that can't be started, because
	// we have to preserve the order of operations in order to ensure the files contain proper data
	for (auto i = newOperations_.begin(); i != newOperations_.end(); i = newOperations_.erase(i)) {
		Operation& operation = *i;
		// Don't start partial-stripe writes if they can be extended in the future.
		// Only the last one can be expanded and only if we accept new data.
		if (i == std::prev(newOperations_.end())
				&& acceptsNewOperations_
				&& !operation.isFullStripe(combinedStripeSize_)) {
			break;
		}
		if (!canStartOperation(operation)) {
			break;
		}
		startOperation(std::move(operation));
		++operationsStarted;
	}
	return operationsStarted;
}

void ChunkWriter::processOperations(uint32_t msTimeout) {
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::processOperations");
	LOG_AVG_TILL_END_OF_SCOPE1("ChunkWriter::processOperations#op", getPendingOperationsCount());
	std::vector<pollfd> pollFds;
	if (dataChainFd_ >= 0) {
		pollFds.push_back(pollfd());
		pollFds.back().fd = dataChainFd_;
		pollFds.back().events = POLLIN;
		pollFds.back().revents = 0;
	}
	for (const auto& pair : executors_) {
		pollFds.push_back(pollfd());
		pollFds.back().fd = pair.first;
		pollFds.back().events = POLLIN;
		if (pair.second->getPendingPacketCount() > 0) {
			pollFds.back().events |= POLLOUT;
		}
		pollFds.back().revents = 0;
	}

	// NOTICE: On Linux there can be pipe descriptor in pollFds.
	// This function can handle it.
	int status = tcppoll(pollFds, msTimeout);
	if (status < 0) {
		throw RecoverableWriteException("Poll error: " + std::string(strerr(tcpgetlasterror())));
	}

	for (pollfd& pollFd : pollFds) {
		if (pollFd.fd == dataChainFd_) {
			if (pollFd.revents & POLLIN) {
				const uint32_t dataFdBufferSize = 1024;
				uint8_t dataFdBuffer[dataFdBufferSize];
				if (read(dataChainFd_, dataFdBuffer, dataFdBufferSize) < 0) {
					lzfs_pretty_syslog(LOG_NOTICE, "read pipe error: %s", strerr(errno));
				}
			}
		} else {
			auto it = executors_.find(pollFd.fd);
			sassert(it != executors_.end());
			WriteExecutor& executor = *it->second;

			if (pollFd.revents & POLLOUT) {
				executor.sendData();
			}

			if (pollFd.revents & POLLIN) {
				std::vector<WriteExecutor::Status> statuses = executor.receiveData();
				for (const auto& status : statuses) {
					processStatus(executor, status);
				}
			}

			if (pollFd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
				throw ChunkserverConnectionException(
						"Write to chunkserver (poll) error", executor.server());
			}

			if (executor.serverTimedOut()) {
				throw ChunkserverConnectionException("Chunkserver timed out", executor.server());
			}
		}
	}
}

uint32_t ChunkWriter::getUnfinishedOperationsCount() {
	return pendingOperations_.size() + newOperations_.size();
}

uint32_t ChunkWriter::getPendingOperationsCount() {
	return pendingOperations_.size();
}


void ChunkWriter::startFlushMode() {
	sassert(acceptsNewOperations_);
	acceptsNewOperations_ = false;
}

void ChunkWriter::dropNewOperations() {
	sassert(acceptsNewOperations_);
	newOperations_.clear();
	acceptsNewOperations_ = false;
}

void ChunkWriter::finish(uint32_t msTimeout) {
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::finish");
	sassert(getPendingOperationsCount() == 0);
	for (auto& pair : executors_) {
		pair.second->addEndPacket();
	}
	Timeout timeout{std::chrono::milliseconds(msTimeout)};
	while (!timeout.expired() && !executors_.empty()) {
		processOperations(timeout.remaining_ms());
		std::vector<int> closedFds;
		for (auto& fdAndExecutor : executors_) {
			int fd = fdAndExecutor.first;
			const WriteExecutor& executor = *fdAndExecutor.second;
			if (executor.getPendingPacketCount() == 0) {
				connector_.endUsingConnection(fd, executor.server());
				closedFds.push_back(fd);
			}
		}
		for (int fd : closedFds) {
			executors_.erase(fd);
		}
	}
}

void ChunkWriter::abortOperations() {
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::abortOperations");
	for (const auto& pair : executors_) {
		if (pair.first < 0) {
			continue;
		}
		tcpclose(pair.first);
	}
	executors_.clear();
}

std::list<WriteCacheBlock> ChunkWriter::releaseJournal() {
	return std::move(journal_);
}

void ChunkWriter::addOperation(WriteCacheBlock&& block) {
	sassert(block.type != WriteCacheBlock::kParityBlock);
	sassert(acceptsNewOperations_);
	sassert(block.chunkIndex == locator_->chunkIndex());
	if (block.type == WriteCacheBlock::kWritableBlock) {
		// Block is writeable until the first try of writing it to chunkservers, ie. until now
		block.type = WriteCacheBlock::kReadOnlyBlock;
	}
	journal_.push_back(std::move(block));
	JournalPosition journalPosition = std::prev(journal_.end());
	if (newOperations_.empty()
			|| !newOperations_.back().isExpandPossible(journalPosition, combinedStripeSize_)) {
		newOperations_.push_back(Operation());
		newOperations_.back().expand(journalPosition);
	} else {
		newOperations_.back().expand(journalPosition);
	}
}

bool ChunkWriter::canStartOperation(const Operation& operation) {
	// Don't start operations which intersect with some pending operation
	// Starting them may result in reading old version of data when calculating new parity.
	for (const auto& writeIdAndOperation : pendingOperations_) {
		const auto& pendingOperation = writeIdAndOperation.second;
		if (operation.collidesWith(pendingOperation)) {
			return false;
		}
	}
	return true;
}

void ChunkWriter::startOperation(Operation operation) {
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::startOperation");
	uint32_t combinedStripe = operation.journalPositions.front()->blockIndex / combinedStripeSize_;
	uint32_t size = operation.journalPositions.front()->size();

	// If the operation is a partial-stripe write, read all the missing blocks first
	std::vector<bool> stripeElementsPresent(combinedStripeSize_);
	for (const auto& position : operation.journalPositions) {
		stripeElementsPresent[position->blockIndex % combinedStripeSize_] = true;
	}
	for (uint32_t indexInStripe = 0; indexInStripe < combinedStripeSize_; ++indexInStripe) {
		LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::startOperation::fillStripe");
		if (stripeElementsPresent[indexInStripe]) {
			continue;
		}
		uint32_t blockIndex = combinedStripe * combinedStripeSize_ + indexInStripe;
		if (blockIndex >= MFSBLOCKSINCHUNK) {
			break;
		}
		ChunkPartType newBlockChunkType = slice_traits::standard::ChunkPartType();
		WriteCacheBlock newBlock = readBlock(blockIndex, newBlockChunkType);
		if (slice_traits::xors::isXorParity(newBlockChunkType)) {
			// Recover data if a parity was read
			uint32_t stripeSize = slice_traits::getStripeSize(newBlockChunkType);
			uint32_t firstBlockInStripe = (blockIndex / stripeSize) * stripeSize;
			for (uint32_t i = firstBlockInStripe; i < firstBlockInStripe + stripeSize; ++i) {
				if (i == newBlock.blockIndex || i >= MFSBLOCKSINCHUNK) {
					continue;
				}
				// Unxor data
				ChunkPartType chunkTypeToXor = slice_traits::standard::ChunkPartType();
				WriteCacheBlock blockToXor = readBlock(i, chunkTypeToXor);
				if (slice_traits::xors::isXorParity(chunkTypeToXor)) {
					throw RecoverableWriteException("Can't recover missing data from parity part");
				}
				blockXor(newBlock.data(), blockToXor.data(), newBlock.size());
			}
		}
		newBlock.from = operation.journalPositions.front()->from;
		newBlock.to = operation.journalPositions.front()->to;
		// Insert the new block into the journal just after the last block of the operation
		auto position = journal_.insert(operation.journalPositions.back(), std::move(newBlock));
		operation.journalPositions.push_back(position);
	}

	// Now operation.journalElements is a complete stripe.
	sassert(operation.isFullStripe(combinedStripeSize_));

	// Send all the data
	OperationId operationId = allocateId();
	for (auto& fdAndExecutor : executors_) {
		WriteExecutor& executor = *fdAndExecutor.second;
		ChunkPartType chunkType = executor.chunkType();
		uint32_t stripeSize = slice_traits::getStripeSize(chunkType);
		sassert(combinedStripeSize_ % stripeSize == 0);
		std::vector<WriteCacheBlock*> blocksToWrite;

		if (slice_traits::isStandard(chunkType)) {
			for (const JournalPosition& position : operation.journalPositions) {
				if (position->type != WriteCacheBlock::kReadBlock) {
					blocksToWrite.push_back(&(*position));
				}
			}
		} else if (slice_traits::xors::isXorParity(chunkType)) {
			LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::startOperation::calculateParity");
			uint32_t substripeCount = combinedStripeSize_ / stripeSize;
			std::vector<WriteCacheBlock*> parityBlocks;
			for (uint32_t i = 0; i < substripeCount; ++i) {
				// Block index will be added later
				operation.parityBuffers.push_back(WriteCacheBlock(
						locator_->chunkIndex(), 0, WriteCacheBlock::kParityBlock));
				parityBlocks.push_back(&operation.parityBuffers.back());
				blocksToWrite.push_back(&operation.parityBuffers.back());
			}
			for (const JournalPosition& position : operation.journalPositions) {
				sassert(position->size() == size);
				uint32_t currentParityBlock =
						(position->blockIndex - combinedStripe * combinedStripeSize_) / stripeSize;
				if (parityBlocks[currentParityBlock]->size() == 0) {
					// We need a block index in ordinary chunk
					//  - it'll be converted to a block index in xor parity later
					parityBlocks[currentParityBlock]->blockIndex = position->blockIndex;
					bool expanded = parityBlocks[currentParityBlock]->expand(
							position->from, position->to, position->data());
					sassert(expanded);
				} else {
					blockXor(parityBlocks[currentParityBlock]->data(), position->data(), size);
				}
			}
		} else {
			for (const JournalPosition& position : operation.journalPositions) {
				if (position->type != WriteCacheBlock::kReadBlock &&
						(position->blockIndex % stripeSize) + 1 == (unsigned)slice_traits::xors::getXorPart(chunkType)) {
					blocksToWrite.push_back(&(*position));
				}
			}
		}

		for (const WriteCacheBlock* block : blocksToWrite) {
			LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::startOperation::addDataPackets");
			WriteId writeId = allocateId();
			writeIdToOperationId_[writeId] = operationId;
			executor.addDataPacket(writeId,
					block->blockIndex / stripeSize, block->from, block->size(), block->data());
			++operation.unfinishedWrites;
		}
	}
	pendingOperations_[operationId] = std::move(operation);
}

WriteCacheBlock ChunkWriter::readBlock(uint32_t blockIndex, ChunkPartType& readFromChunkType) {
	Timeout timeout{std::chrono::seconds(1)};
	LOG_AVG_TILL_END_OF_SCOPE0("ChunkWriter::readBlock");

	// Find a server from which we will be able to read the block
	ChunkTypeWithAddress source_type_with_address;
	ChunkPartType sourceChunkType = slice_traits::standard::ChunkPartType();
	for (const auto& fdAndExecutor : executors_) {
		const auto& executor = *fdAndExecutor.second;
		ChunkPartType chunkType = executor.chunkType();
		if (slice_traits::isStandard(chunkType)) {
			source_type_with_address = executor.chunkTypeWithAddress();
			sourceChunkType = chunkType;
			break;
		} else {
			sassert(slice_traits::isXor(chunkType));
			if (slice_traits::xors::isXorParity(chunkType)) {
				if (source_type_with_address == ChunkTypeWithAddress() ||
						(slice_traits::xors::isXorParity(sourceChunkType)
						&& slice_traits::xors::getXorLevel(chunkType) < slice_traits::xors::getXorLevel(sourceChunkType))) {
					// Find a parity with the smallest XOR level
					source_type_with_address = executor.chunkTypeWithAddress();
					sourceChunkType = chunkType;
				} else {
					continue;
				}
			} else if (blockIndex % slice_traits::xors::getXorLevel(chunkType) + 1 == (unsigned)slice_traits::xors::getXorPart(chunkType)) {
				source_type_with_address = executor.chunkTypeWithAddress();
				sourceChunkType = chunkType;
				break;
			}
		}
	}
	if (source_type_with_address == ChunkTypeWithAddress()) {
		throw RecoverableWriteException("No server to read block " + std::to_string(blockIndex));
	}

	// Prepare the read operation
	uint32_t stripe = blockIndex;
	if (slice_traits::isXor(sourceChunkType)) {
		stripe /= slice_traits::xors::getXorLevel(sourceChunkType);
	}
	ReadPlan::ReadOperation readOperation;
	readOperation.request_offset = stripe * MFSBLOCKSIZE;
	readOperation.request_size = MFSBLOCKSIZE;
	readOperation.buffer_offset = 0;

	// Connect to the chunkserver and execute the read operation
	int fd = connector_.startUsingConnection(source_type_with_address.address, timeout);
	try {
		WriteCacheBlock block(locator_->chunkIndex(), blockIndex, WriteCacheBlock::kReadBlock);
		block.from = 0;
		block.to = MFSBLOCKSIZE;
		ReadOperationExecutor readExecutor(readOperation,
				locator_->locationInfo().chunkId, locator_->locationInfo().version,
				sourceChunkType, source_type_with_address.address,
				source_type_with_address.chunkserver_version, fd, block.data());
		readExecutor.sendReadRequest(timeout);
		readExecutor.readAll(timeout);
		connector_.endUsingConnection(fd, source_type_with_address.address);
		readFromChunkType = sourceChunkType;
		return block;
	} catch (...) {
		tcpclose(fd);
		throw;
	}
}

void ChunkWriter::processStatus(const WriteExecutor& executor,
		const WriteExecutor::Status& status) {
	if (status.chunkId != locator_->locationInfo().chunkId) {
		throw ChunkserverConnectionException(
				"Received inconsistent write status message"
				", expected chunk " + std::to_string(locator_->locationInfo().chunkId) +
				", got chunk " + std::to_string(status.chunkId),
				executor.server());
	}
	if (status.status != LIZARDFS_STATUS_OK) {
		throw RecoverableWriteException("Chunk write error", status.status);
	}

	// Translate writeId to operationId
	OperationId operationId = 0;
	if (status.writeId != 0) {
		try {
			operationId = writeIdToOperationId_.at(status.writeId);
			writeIdToOperationId_.erase(status.writeId);
		} catch (std::out_of_range &e) {
			throw RecoverableWriteException(
				"Chunk write error: unexpected status for operation #" +
				std::to_string(status.writeId));
		}
	} else if (pendingOperations_.count(0) == 0) {
		throw RecoverableWriteException("Chunk write error: unexpected status for WRITE_INIT");
	}

	sassert(pendingOperations_.count(operationId) == 1);
	auto& operation = pendingOperations_[operationId];
	if (--operation.unfinishedWrites == 0) {
		// Operation has just finished: update file size if changed and delete the operation
		if (operationId != 0) {
			// This was a WRITE_DATA operation, not WRITE_INIT
			if (operation.offsetOfEnd > locator_->locationInfo().fileLength) {
				locator_->updateFileLength(operation.offsetOfEnd);
			}
			for (const auto& position : operation.journalPositions) {
				journal_.erase(position);
			}
		}
		pendingOperations_.erase(operationId);
	}
}
