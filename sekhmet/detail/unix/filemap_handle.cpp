//
// Created by switchblade on 2022-04-07.
//

#include "filemap_handle.hpp"

#include <bit>
#include <fcntl.h>

#include <sys/mman.h>

namespace sek::detail
{
	static auto page_size() noexcept { return sysconf(_SC_PAGE_SIZE); }
	static std::size_t file_size(int fd) noexcept
	{
		struct stat st;
		return !fstat(fd, &st) ? st.st_size : 0;
	}

	filemap_handle::native_handle_type filemap_handle::handle_from_view(void *ptr) noexcept
	{
		auto int_ptr = std::bit_cast<std::intptr_t>(ptr);
		return std::bit_cast<void *>(int_ptr - (int_ptr % page_size()));
	}

	void filemap_handle::init(int fd, std::ptrdiff_t offset, std::size_t size, filemap_openmode mode, const char *)
	{
		int prot = (mode & filemap_in ? PROT_READ : 0) | (mode & filemap_out ? PROT_WRITE : 0);

		/* Adjust offset to be a multiple of page size. */
		auto offset_diff = offset % page_size();
		auto real_offset = offset - offset_diff;

		/* Get the actual size from the file descriptor if size == 0. */
		std::size_t real_size;
		if (!size) [[unlikely]]
		{
			if ((real_size = file_size(fd)) == 0) [[unlikely]]
				throw filemap_error("Failed to get file size");
			real_size = (size = real_size - offset) + offset_diff;
		}
		else
			real_size = size + static_cast<std::size_t>(offset_diff);

		view_ptr = mmap(nullptr, real_size, prot, MAP_SHARED, fd, real_offset);
		if (!view_ptr) [[unlikely]]
			throw filemap_error("Failed to mmap file");

		/* Offset might not be the same as the start position, need to adjust the pointer. */
		view_ptr = std::bit_cast<void *>(std::bit_cast<std::intptr_t>(view_ptr) + offset_diff);
		map_size = size;
	}
	filemap_handle::filemap_handle(const char *path, std::ptrdiff_t offset, std::size_t size, filemap_openmode mode, const char *name)
	{
		struct raii_file
		{
			constexpr explicit raii_file(int fd) noexcept : fd(fd) {}
			~raii_file() { close(fd); }

			int fd;
		};

		int flags = O_RDONLY;
		if ((mode & filemap_in) && (mode & filemap_out))
			flags = O_RDWR;
		else if (mode & filemap_in)
			flags = O_WRONLY;
		auto file = raii_file{open(path, flags | O_CLOEXEC)};
		if (file.fd < 0) [[unlikely]]
			throw filemap_error("Failed to open file descriptor");

		init(file.fd, offset, size, mode, name);
	}
	bool filemap_handle::reset() noexcept
	{
		if (view_ptr) [[likely]]
		{
			auto int_ptr = std::bit_cast<std::intptr_t>(view_ptr);
			auto diff = int_ptr % page_size();
			return !munmap(std::bit_cast<void *>(int_ptr - diff), map_size + static_cast<std::size_t>(diff));
		}
		return false;
	}
	void filemap_handle::flush(std::size_t n) const
	{
		auto int_ptr = std::bit_cast<std::intptr_t>(view_ptr);
		auto diff = int_ptr % page_size();
		if (msync(std::bit_cast<void *>(int_ptr - diff), n + static_cast<size_t>(diff), MS_SYNC | MS_INVALIDATE)) [[unlikely]]
		{
			switch (errno)
			{
				case EBUSY: throw filemap_error("Mapped file is busy");
				case ENOMEM:
				case EINVAL: throw filemap_error("Bad mapping handle");
				default: throw filemap_error("Call to `msync` failed");
			}
		}
	}
}	 // namespace sek::detail
