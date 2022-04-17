//
// Created by switchblade on 2022-04-14.
//

#pragma once

#include "archive_traits.hpp"

namespace sek::serialization
{
	/** @brief Archive manipulator used to specify an explicit name for an entry. */
	template<typename T>
	struct named_entry
	{
	private:
		constexpr static bool noexcept_fwd = noexcept(T(std::forward<T>(std::declval<T &&>())));

	public:
		named_entry() = delete;

		/** Constructs a named entry manipulator from a name and a perfectly-forwarded value.
		 * @param name Name of the entry.
		 * @param value Value forwarded by the manipulator. */
		constexpr named_entry(std::string_view name, T &&value) noexcept(noexcept_fwd)
			: name(name), value(std::forward<T>(value))
		{
		}
		/** @copydoc named_entry */
		constexpr named_entry(const char *name, T &&value) noexcept(noexcept(noexcept_fwd))
			: name(name), value(std::forward<T>(value))
		{
		}

		std::string_view name;
		T value;
	};

	template<typename T>
	named_entry(std::string_view name, T &&value) -> named_entry<T>;
	template<typename T>
	named_entry(const char *name, T &&value) -> named_entry<T>;

	namespace detail
	{
		template<typename A, typename T>
		concept named_entry_input = requires(A &archive, std::remove_cvref_t<T> &data)
		{
			input_archive<A, T>;
			input_archive<A, decltype(named_entry{std::declval<std::string_view>(), data})>;
			input_archive<A, decltype(named_entry{std::declval<const char *>(), data})>;
		};
		template<typename A, typename T>
		concept named_entry_output = requires(A &archive, const std::remove_cvref_t<T> &data)
		{
			output_archive<A, T>;
			output_archive<A, decltype(named_entry{std::declval<std::string_view>(), data})>;
			output_archive<A, decltype(named_entry{std::declval<const char *>(), data})>;
		};
	}	 // namespace detail

	/** @brief Concept satisfied only if archive `A` supports input or output of named entries of `T`. */
	template<typename A, typename T>
	concept named_entry_archive = detail::named_entry_input<A, T> || detail::named_entry_output<A, T>;

	/** @brief Constant used as a dynamic size value for array & object entry manipulators. */
	constexpr auto dynamic_size = std::numeric_limits<std::size_t>::max();

	/** @brief Archive manipulator used to switch an archive to array IO mode and read/write array size.
	 * @note If the archive does not support fixed-size arrays, size will be left unmodified. */
	template<typename T>
	requires std::integral<std::decay_t<T>>
	struct array_entry
	{
		/** Constructs an array entry manipulator from a perfectly-forwarded array size.
		 * @param value Size of the array forwarded by the manipulator. */
		constexpr explicit array_entry(T &&value) noexcept : value(std::forward<T>(value)) {}

		T value;
	};
	template<typename T>
	array_entry(T &&value) -> array_entry<T>;

	/** @brief Archive manipulator used to switch an archive to array IO mode and read/write object size.
	 * @note If the archive does not support fixed-size objects, size will be left unmodified. */
	template<typename T>
	requires std::integral<std::decay_t<T>>
	struct object_entry
	{
		/** Constructs an array entry manipulator from a perfectly-forwarded array size.
		 * @param value Size of the array forwarded by the manipulator. */
		constexpr explicit object_entry(T &&value) noexcept : value(std::forward<T>(value)) {}

		T value;
	};
	template<typename T>
	object_entry(T &&value) -> object_entry<T>;

	namespace detail
	{
		template<typename A>
		concept fixed_size_input = requires(A &archive, std::size_t &size)
		{
			input_archive<A, decltype(array_entry{size})>;
			input_archive<A, decltype(object_entry{size})>;
		};
		template<typename A>
		concept fixed_size_output = requires(A &archive, const std::size_t &size)
		{
			output_archive<A, decltype(array_entry{size})>;
			output_archive<A, decltype(object_entry{size})>;
		};
	}	 // namespace detail

	/** @brief Concept satisfied only if archive `A` supports input or output of fixed-size sequences. */
	template<typename A>
	concept fixed_size_archive = detail::fixed_size_input<A> || detail::fixed_size_output<A>;

	/** @brief Archive manipulator used to change archive's pretty-printing mode.
	 * @note If the archive does not support pretty-printing, this manipulator will be ignored. */
	struct pretty_print
	{
		pretty_print() = delete;

		/** Initializes the modifier to the specific pretty-print mode.
		 * @param value If set to true, pretty-printing will be enabled, otherwise pretty-printing will be disabled. */
		constexpr explicit pretty_print(bool value) noexcept : value(value) {}

		bool value;
	};
}	 // namespace sek::serialization