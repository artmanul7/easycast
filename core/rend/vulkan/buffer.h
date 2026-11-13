/*
 *  Created on: Oct 3, 2019

	Copyright 2019 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "vulkan.h"
#include "vmallocator.h"
#include "utils.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

struct BufferData
{
	BufferData(vk::DeviceSize size, vk::BufferUsageFlags usage,
			vk::MemoryPropertyFlags propertyFlags =
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	~BufferData()
	{
		buffer.reset();
	}

	void upload(u32 size, const void *data, u32 bufOffset = 0) const
	{
		verify(bufOffset + size <= bufferSize);

		void* dataPtr = (u8 *)allocation.MapMemory() + bufOffset;
		memcpy(dataPtr, data, size);
		allocation.UnmapMemory();
	}

	void upload(size_t count, const u32 *sizes, const void * const *data, u32 bufOffset = 0) const
	{
		u32 totalSize = 0;
		for (size_t i = 0; i < count; i++)
			totalSize += sizes[i];
		verify(bufOffset + totalSize <= bufferSize);
		const auto easycastT0 = std::chrono::steady_clock::now();	// EasyCast: packed upload timing
		void* dataPtr = (u8 *)allocation.MapMemory() + bufOffset;
		for (size_t i = 0; i < count; i++)
		{
			if (data[i] != nullptr)
				memcpy(dataPtr, data[i], sizes[i]);
			dataPtr = (u8 *)dataPtr + sizes[i];
		}
		if (allocation.IsPersistent())
			allocation.Flush(bufOffset, totalSize);	// EasyCast: only the written range
		else
			allocation.UnmapMemory();
		easycastUploadStat(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - easycastT0).count(), totalSize);
	}

	static void easycastUploadStat(long long us, u32 bytes)
	{
		static long long sum = 0, mx = 0, kb = 0, n = 0;
		static auto t0 = std::chrono::steady_clock::now();
		sum += us; kb += bytes / 1024; n++; if (us > mx) mx = us;
		auto now = std::chrono::steady_clock::now();
		if (now - t0 >= std::chrono::seconds(5))
		{
			std::fprintf(stderr, "EASYCAST_UPLOAD calls=%lld avg_us=%lld max_us=%lld kb_avg=%lld\n", n, n ? sum / n : 0, mx, n ? kb / n : 0);
			sum = mx = kb = n = 0;
			t0 = now;
		}
	}

	void download(u32 size, void *data, u32 bufOffset = 0) const
	{
		verify(bufOffset + size <= bufferSize);

		void* dataPtr = (u8 *)allocation.MapMemory() + bufOffset;
		memcpy(data, dataPtr, size);
		allocation.UnmapMemory();
	}

	void *MapMemory() const
	{
		return allocation.MapMemory();
	}
	void UnmapMemory() const
	{
		allocation.UnmapMemory();
	}

	vk::UniqueBuffer buffer;
	vk::DeviceSize bufferSize;
	Allocation allocation;

private:
	vk::BufferUsageFlags    m_usage;
};

class BufferPacker
{
public:
	BufferPacker();
	void reset();

	vk::DeviceSize addUniform(const void *p, size_t size) {
		return add(p, size, uniformAlignment);
	}

	vk::DeviceSize addStorage(const void *p, size_t size) {
		return add(p, size, storageAlignment);
	}

	vk::DeviceSize add(const void *p, size_t size, u32 alignment = 4)
	{
		u32 padding = align(offset, std::max(4u, alignment));
		if (padding != 0)
		{
			chunks.push_back(nullptr);
			chunkSizes.push_back(padding);
			offset += padding;
		}
		vk::DeviceSize start = offset;
		chunks.push_back(p);
		chunkSizes.push_back(size);
		offset += size;

		return start;
	}

	void upload(BufferData& bufferData, u32 bufOffset = 0)
	{
		if (!chunks.empty())
			bufferData.upload(chunks.size(), &chunkSizes[0], &chunks[0], bufOffset);
	}

	vk::DeviceSize size() const {
		return offset;
	}

private:
	std::vector<const void *> chunks;
	std::vector<u32> chunkSizes;
	vk::DeviceSize offset = 0;
	vk::DeviceSize uniformAlignment;
	vk::DeviceSize storageAlignment;
};
