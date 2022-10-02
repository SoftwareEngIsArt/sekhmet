/*
 * Created by switchblade on 10/08/22
 */

#pragma once

#include "../../../detail/basic_pool.hpp"
#include "../../../expected.hpp"
#include "info.hpp"

namespace sek::detail
{
	class archive_package : public package_info
	{
	protected:
		struct archive_slice
		{
			std::uint64_t offset;
			std::uint64_t size;		/* Compressed size. */
			std::uint64_t src_size; /* Decompressed size. */
			std::uint32_t frames;	/* Amount of compressed frames used (0 if not compressed). */
		};
		struct archive_info : asset_info
		{
			archive_slice asset_slice;
			archive_slice meta_slice;
		};

	public:
		explicit archive_package(const uri &location) : package_info(location) {}
		explicit archive_package(uri &&location) noexcept : package_info(std::move(location)) {}
		~archive_package() override { destroy_all(); }

		[[nodiscard]] asset_info *alloc_info() final { return m_pool.allocate(); }
		void dealloc_info(asset_info *info) final { m_pool.deallocate(static_cast<archive_info *>(info)); }
		void destroy_info(asset_info *info) final { std::destroy_at(static_cast<archive_info *>(info)); }

		expected<asset_source, std::error_code> open_asset(const asset_info *) const noexcept final;
		expected<asset_source, std::error_code> open_metadata(const asset_info *) const noexcept final;

		[[nodiscard]] constexpr bool has_metadata(const asset_info *info) const noexcept final
		{
			return static_cast<const archive_info *>(info)->meta_slice.offset != 0;
		}

	protected:
		virtual expected<asset_source, std::error_code> open_at(archive_slice) const noexcept = 0;

	private:
		sek::detail::basic_pool<archive_info> m_pool;
	};

	class flat_package final : public archive_package
	{
	public:
		explicit flat_package(const uri &location) : archive_package(location) {}
		explicit flat_package(uri &&location) noexcept : archive_package(std::move(location)) {}

		~flat_package() final = default;

	private:
		expected<asset_source, std::error_code> open_at(archive_slice) const noexcept override;
	};
	class zstd_package final : public archive_package
	{
	public:
		explicit zstd_package(const uri &location) : archive_package(location) {}
		explicit zstd_package(uri &&location) noexcept : archive_package(std::move(location)) {}

		~zstd_package() final = default;

	private:
		expected<asset_source, std::error_code> open_at(archive_slice) const noexcept override;
	};
}	 // namespace sek::detail