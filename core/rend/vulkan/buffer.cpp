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
#include "buffer.h"
#include "vulkan_context.h"

BufferData::BufferData(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags propertyFlags)
	: bufferSize(size), m_usage(usage)
{
	VulkanContext *context = VulkanContext::Instance();
	buffer = context->GetDevice().createBufferUnique(vk::BufferCreateInfo(vk::BufferCreateFlags(), size, usage));
	VmaAllocationCreateInfo allocInfo {};
	if (propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
	{
		allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
	else
	{
		// FIXME VMA_ALLOCATION_CREATE_MAPPED_BIT ?
#ifdef __APPLE__
		// cpu memory management is fucked up with moltenvk
		allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		// host coherent memory not supported on apple platforms
		propertyFlags &= ~vk::MemoryPropertyFlagBits::eHostCoherent;
#endif
		if (propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
		{
			allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			static const bool easycastPersist = [] {	// EasyCast 0.2.6: keep host-visible buffers mapped
				const char *e = std::getenv("EASYCAST_PERSIST_MAP");
				return !(e != nullptr && e[0] == '0');
			}();
			if (easycastPersist)
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
			if (propertyFlags & vk::MemoryPropertyFlagBits::eHostCached)
				allocInfo.preferredFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
			if (propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
				allocInfo.preferredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		}
	}
	allocation = context->GetAllocator().AllocateForBuffer(*buffer, allocInfo);
	if (propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
	{
		static u32 loggedMask = 0;	// one line per distinct memory type
		const u32 bit = 1u << (allocation.MemoryFlags() & 31);
		if ((loggedMask & bit) == 0)
		{
			loggedMask |= bit;
			std::fprintf(stderr, "EASYCAST_UPLOAD memory_flags=0x%x persistent=%d\n", allocation.MemoryFlags(),
					(allocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) ? 1 : 0);
		}
	}
}

BufferPacker::BufferPacker()
{
	reset();
}

void BufferPacker::reset()
{
	offset = 0;
	chunks.clear();
	chunkSizes.clear();
	uniformAlignment = VulkanContext::Instance()->GetUniformBufferAlignment();
	storageAlignment = VulkanContext::Instance()->GetStorageBufferAlignment();
}
