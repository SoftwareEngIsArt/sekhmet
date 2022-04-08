//
// Created by switchblade on 2022-04-04.
//

#define _CRT_SECURE_NO_WARNINGS

#include "assets.hpp"

#include <cstdio>
#include <memory>

#ifdef SEK_OS_WIN
#define MANIFEST_FILE_NAME L".manifest"
#define OS_FOPEN(path, mode) _wfopen(path, L##mode)
#else
#define MANIFEST_FILE_NAME ".manifest"
#define OS_FOPEN(path, mode) fopen(path, mode)
#endif

namespace sek
{
	template<>
	SEK_API_EXPORT std::atomic<asset_repository *> &basic_service<asset_repository>::global_ptr() noexcept
	{
		static std::atomic<asset_repository *> value;
		return value;
	}
	std::shared_mutex &asset_repository::global_mtx() noexcept
	{
		static std::shared_mutex value;
		return value;
	}

	namespace detail
	{
		void serialize(adt::node &node, const loose_asset_record &record)
		{
			node = adt::table{
				{"id", record.id},
				{"tags", record.tags},
				{"path", record.file_path.string()},
			};

			if (!record.metadata_path.empty()) node["metadata"] = record.metadata_path.string();
		}
		void deserialize(const adt::node &node, loose_asset_record &record)
		{
			node.at("id").get(record.id);
			node.at("tags").get(record.tags);
			record.file_path.assign(node.at("path").as_string());

			if (node.as_table().contains("metadata")) record.metadata_path.assign(node.at("metadata").as_string());
		}
		void serialize(adt::node &node, const archive_asset_record &record)
		{
			node = adt::sequence{
				record.id,
				record.tags,
				record.file_offset,
				record.file_size,
				record.metadata_offset,
				record.metadata_size,
			};
		}
		void deserialize(const adt::node &node, archive_asset_record &record)
		{
			if (node.as_sequence().size() >= 6) [[likely]]
			{
				node[0].get(record.id);
				node[1].get(record.tags);
				node[2].get(record.file_offset);
				node[3].get(record.file_size);
				node[4].get(record.metadata_offset);
				node[5].get(record.metadata_size);
			}
			else
				throw adt::node_error("Invalid archive record size");
		}

		void serialize_impl(adt::node &node, const package_fragment &fragment)
		{
			if (fragment.is_loose())
				node.at("assets").set(fragment.loose_assets);
			else
				node.at("assets").set(fragment.archive_assets);
		}
		void serialize(adt::node &node, const package_fragment &fragment)
		{
			node = adt::table{{"assets", adt::node{}}};
			serialize_impl(node, fragment);
		}
		void serialize(adt::node &node, const master_package &package)
		{
			node = adt::table{
				{"master", true},
				{"assets", adt::node{}},
			};

			if (!package.fragments.empty()) [[likely]]
			{
				auto &fragments = node.as_table().emplace("fragments", adt::sequence{}).first->second.as_sequence();
				for (auto &fragment : package.fragments)
					fragments.emplace_back(relative(fragment.path, package.path).string());
			}

			serialize_impl(node, package);
		}

		struct package_info
		{
			adt::node manifest = {};
			package_fragment::flags_t flags = {};
		};

		static package_info get_package_info(const std::filesystem::path &path)
		{
			package_info result;
			FILE *manifest_file;
			if (is_directory(path))
			{
				result.flags = package_fragment::LOOSE_PACKAGE;
				if ((manifest_file = OS_FOPEN((path / MANIFEST_FILE_NAME).c_str(), "r")) != nullptr) [[likely]]
				{
					/* TODO: read TOML manifest. */
				}
			}
			else if ((manifest_file = OS_FOPEN(path.c_str(), "rb")) != nullptr) [[likely]]
			{
				/* Check that the package has a valid signature. */
				constexpr auto sign_size = sizeof(SEK_PACKAGE_SIGNATURE);
				char sign[sign_size];
				if (fread(sign, 1, sign_size, manifest_file) == sign_size && !memcmp(sign, SEK_PACKAGE_SIGNATURE, sign_size))
				{
					/* TODO: read UBJson manifest. */
				}
			}
			return result;
		}

		void deserialize(const adt::node &node, package_fragment &fragment)
		{
			if (fragment.is_loose())
				node.at("assets").get(fragment.loose_assets);
			else
				node.at("assets").get(fragment.archive_assets);
		}
		void deserialize(const adt::node &node, master_package &package)
		{
			deserialize(node, static_cast<package_fragment &>(package));
			if (node.as_table().contains("fragments"))
			{
				auto &fragments = node.at("fragments").as_sequence();
				package.fragments.reserve(fragments.size());
				for (auto &fragment : fragments)
				{
					auto path = package.path / fragment.as_string();
					auto info = get_package_info(path);
					deserialize(info.manifest, package.add_fragment(std::move(path), info.flags));
				}
			}
		}

		master_package *load_package(std::filesystem::path &&path)
		{
			try
			{
				auto info = get_package_info(path);
				auto &table = info.manifest.as_table();
				if (auto flag_iter = table.find("master"); flag_iter != table.end() && flag_iter->second.as_bool())
				{
					auto package = std::make_unique<master_package>(std::move(path), info.flags);
					deserialize(info.manifest, *package);
					return package.release();
				}
			}
			catch (adt::node_error &)
			{
				/* Only deserialization exceptions are recoverable, since they indicate an invalid package and thus will return nullptr.
				 * Any other exceptions are either caused by fatal errors or by filesystem errors and are thus non-recoverable. */
			}
			return nullptr;
		}
	}	 // namespace detail
}	 // namespace sek