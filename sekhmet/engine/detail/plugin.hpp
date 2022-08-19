/*
 * Created by switchblade on 2022-04-24
 */

#pragma once

#include <vector>

#include "sekhmet/detail/define.h"
#include "sekhmet/event.hpp"
#include "sekhmet/static_string.hpp"
#include "sekhmet/version.hpp"

namespace sek::engine
{
	namespace detail
	{
		struct plugin_info
		{
			consteval plugin_info(version engine_ver, version plugin_ver, std::string_view id) noexcept
				: engine_ver(engine_ver), plugin_ver(plugin_ver), id(id)
			{
			}

			/** Version of the engine the plugin was compiled for. */
			const version engine_ver;
			/** Version of the plugin. */
			const version plugin_ver;
			/** Id of the plugin. */
			const std::string_view id;
		};
		struct plugin_data
		{
			enum status_t
			{
				INITIAL,
				DISABLED,
				ENABLED,
			};

			SEK_API static void load(plugin_data *data, void (*init)(void *));
			static void load_impl(plugin_data *data, void (*init)(void *));

			SEK_API static void unload(plugin_data *data);
			static void unload_impl(plugin_data *data);

			explicit plugin_data(plugin_info info) noexcept : info(info) {}

			[[nodiscard]] bool enable() const
			{
				bool result;
				on_enable.dispatch([&result](bool b) { return (result = b); });
				return result;
			}
			void disable() const { on_disable(); }

			/** Compile-time information about this plugin. */
			const plugin_info info;
			/** Event dispatched when a plugin is enabled by the engine. */
			event<bool(void)> on_enable;
			/** Event dispatched when a plugin is disabled by the engine. */
			event<void(void)> on_disable;

			status_t status;
		};

		template<typename Child>
		class plugin_base : plugin_data
		{
		public:
			static Child instance;

			explicit plugin_base(plugin_info info) noexcept : plugin_data(info)
			{
				plugin_data::load(this, [](void *p) { static_cast<Child *>(p)->init(); });
			}

			using plugin_data::info;
			using plugin_data::on_disable;
			using plugin_data::on_enable;
		};
	}	 // namespace detail

	/** @brief Handle used to reference and manage plugins. */
	class plugin
	{
	public:
		/** Returns a vector of all currently loaded plugins. */
		SEK_API static std::vector<plugin> get_loaded();
		/** Returns a vector of all currently enabled plugins. */
		SEK_API static std::vector<plugin> get_enabled();

		/** Returns a plugin using it's id. If such plugin does not exist, returns an empty handle. */
		SEK_API static plugin get(std::string_view id);

	private:
		constexpr explicit plugin(detail::plugin_data *data) noexcept : m_data(data) {}

	public:
		/** Initializes an empty plugin handle. */
		constexpr plugin() noexcept = default;

		/** Checks if the plugin handle is empty. */
		[[nodiscard]] constexpr bool empty() const noexcept { return m_data == nullptr; }
		/** @copydoc empty */
		[[nodiscard]] constexpr operator bool() const noexcept { return !empty(); }

		/** Returns id of the plugin. */
		[[nodiscard]] constexpr std::string_view id() const noexcept { return m_data->info.id; }
		/** Returns engine version of the plugin. */
		[[nodiscard]] constexpr version engine_ver() const noexcept { return m_data->info.engine_ver; }

		/** Checks if the plugin is enabled. */
		[[nodiscard]] SEK_API bool enabled() const noexcept;
		/** Enables the plugin and invokes it's `on_enable` member function.
		 * @returns true on success, false otherwise.
		 * @note Plugin will fail to enable if it is already enabled or not loaded or if `on_enable` returned false or threw an exception. */
		[[nodiscard]] SEK_API bool enable() const noexcept;
		/** Disables the plugin and invokes it's `on_disable` member function.
		 * @returns true on success, false otherwise.
		 * @note Plugin will fail to disable if it is not enabled or not loaded. */
		[[nodiscard]] SEK_API bool disable() const noexcept;

		[[nodiscard]] constexpr auto operator<=>(const plugin &) const noexcept = default;
		[[nodiscard]] constexpr bool operator==(const plugin &) const noexcept = default;

	private:
		detail::plugin_data *m_data = nullptr;
	};
}	 // namespace sek::engine

namespace impl
{
	template<sek::basic_static_string Id>
	struct plugin_instance : sek::engine::detail::plugin_base<plugin_instance<Id>>
	{
		friend class sek::engine::detail::plugin_base<plugin_instance>;

		plugin_instance();

	private:
		void init();
	};
}	 // namespace impl

/** @brief Macro used to reference the internal unique type of a plugin.
 * @param id String id used to uniquely identify a plugin.
 * @note Referenced plugin must be declared first. */
#define SEK_PLUGIN(id) impl::plugin_instance<(id)>
/** @brief Macro used to define an instance of a plugin.
 * @param id String id used to uniquely identify a plugin.
 * @param ver Version of the plugin in the following format: `"<major>.<minor>.<patch>"`.
 *
 * @example
 * @code{.cpp}
 * SEK_PLUGIN_INSTANCE("my_plugin", "0.1.2")
 * {
 * 	printf("%s is initializing! version: %d.%d.%d\n",
 * 		   info.id.data(),
 * 		   info.engine_ver.major(),
 * 		   info.engine_ver.minor(),
 * 		   info.engine_ver.patch());
 *
 * 	on_enable += +[]()
 * 	{
 * 		printf("Enabling my_plugin\n");
 * 		return true;
 * 	};
 * 	on_disable += +[]() { printf("Disabling my_plugin\n"); };
 * }
 * @endcode */
#define SEK_PLUGIN_INSTANCE(id, ver)                                                                                   \
	static_assert(SEK_ARRAY_SIZE(id), "Plugin id must not be empty");                                                  \
	template<>                                                                                                         \
	SEK_PLUGIN(id)::plugin_instance() : plugin_base({sek::version{SEK_ENGINE_VERSION}, sek::version{ver}, (id)})       \
	{                                                                                                                  \
	}                                                                                                                  \
	template<>                                                                                                         \
	impl::plugin_instance<(id)> sek::engine::detail::plugin_base<impl::plugin_instance<(id)>>::instance = {};          \
                                                                                                                       \
	template<>                                                                                                         \
	void impl::plugin_instance<(id)>::init()

#if defined(SEK_PLUGIN_NAME) && defined(SEK_PLUGIN_VERSION)
/** @brief Macro used to define a plugin with name & version specified by the current plugin project.
 * See `SEK_PLUGIN_INSTANCE` for details. */
#define SEK_PROJECT_PLUGIN_INSTANCE() SEK_PLUGIN_INSTANCE(SEK_PLUGIN_NAME, SEK_PLUGIN_VERSION)
/** @brief Macro used to reference the type of a plugin with name & version specified by the current plugin project.
 * See `SEK_PLUGIN` for details. */
#define SEK_PROJECT_PLUGIN SEK_PLUGIN(SEK_PLUGIN_NAME)
#endif
// clang-format on
