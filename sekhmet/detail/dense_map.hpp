/*
 * Created by switchblade on 07/05/22
 */

#pragma once

#include <iterator>
#include <stdexcept>

#include "dense_hash_table.hpp"
#include "table_util.hpp"

namespace sek
{
	/** @brief One-to-one dense table based associative container providing fast iteration & insertion.
	 *
	 * Dense maps are implemented via a closed-addressing contiguous (packed) storage hash table.
	 * This allows for efficient iteration & insertion (iterate over a packed array & push on top of the array).
	 * Dense maps may invalidate iterators on insertion due to the internal packed storage being re-sized.
	 * On erasure, iterators to the erased element and elements after the erased one may be invalidated.
	 *
	 * @note Dense map iterators do not return references/pointers to pairs, instead they return special proxy
	 * reference & pointer types. This comes from a requirement for dense array elements to be assignable,
	 * thus using pairs where one of the elements is const is not possible.
	 * @note Dense maps do not provide node functionality, since the data is laid out in a contiguous packed array.
	 *
	 * @tparam K Type of objects used as keys.
	 * @tparam M Type of objects associated with keys.
	 * @tparam KeyHash Functor used to generate hashes for keys. By default uses `default_hash` which calls static
	 * non-member `hash` function via ADL if available, otherwise invokes `std::hash`.
	 * @tparam KeyComp Predicate used to compare keys.
	 * @tparam Alloc Allocator used for the map. */
	template<typename K, typename M, typename KeyHash = default_hash, typename KeyComp = std::equal_to<K>, typename Alloc = std::allocator<std::pair<const K, M>>>
	class dense_map
	{
	public:
		typedef K key_type;
		typedef M mapped_type;
		typedef std::pair<const key_type, mapped_type> value_type;

	private:
		/* Since we cannot store value_type directly within the table (const key cannot be assigned by the dense
		 * vector), need to do this ugliness with a proxy reference/value & proxy pointer types. */

		constexpr static auto enable_three_way =
			requires(value_type a, value_type b)
		{
			std::compare_three_way{}(a.first, b.first);
			std::compare_three_way{}(a.second, b.second);
		};
		constexpr static auto enable_equal =
			requires(value_type a, value_type b)
		{
			std::equal_to<>{}(a.first, b.first);
			std::equal_to<>{}(a.second, b.second);
		};

		// clang-format off
		template<bool Const>
		class value_pointer;
		template<bool Const>
		class value_reference
		{
		public:
			value_reference() = delete;

			/* Conversions from relevant references. */
			constexpr value_reference(value_type &ref) noexcept : first{ref.first}, second{ref.second} {}
			constexpr value_reference(std::pair<key_type, mapped_type> &ref) noexcept : first{ref.first}, second{ref.second} {}
			constexpr value_reference(const value_type &ref) noexcept requires(Const) : first{ref.first}, second{ref.second} {}
			constexpr value_reference(const std::pair<key_type, mapped_type> &ref) noexcept requires(Const) : first{ref.first}, second{ref.second} {}

			/* Here overloading operator& is fine, since we want to use value_pointer for pointers. */
			[[nodiscard]] constexpr value_pointer<Const> operator&() const noexcept { return value_pointer<Const>{this}; }
			[[nodiscard]] constexpr operator value_type() const noexcept { return {first, second}; }

			[[nodiscard]] constexpr auto operator<=>(const value_reference &other) const noexcept requires enable_three_way
			{
				using res_t = typename std::common_comparison_category<decltype(std::compare_three_way{}(first, other.first)),
				                                                       decltype(std::compare_three_way{}(second, other.second))>::type;

				if (const res_t cmp_first = std::compare_three_way{}(first, other.first); cmp_first == std::strong_ordering::equal)
					return res_t{std::compare_three_way{}(second, other.second)};
				else
					return cmp_first;
			}
			[[nodiscard]] constexpr auto operator<=>(const value_type &other) const noexcept requires enable_three_way
			{
				using res_t = typename std::common_comparison_category<decltype(std::compare_three_way{}(first, other.first)),
																	   decltype(std::compare_three_way{}(second, other.second))>::type;

				if (const res_t cmp_first = std::compare_three_way{}(first, other.first); cmp_first == std::strong_ordering::equal)
					return res_t{std::compare_three_way{}(second, other.second)};
				else
					return cmp_first;
			}
			[[nodiscard]] constexpr bool operator==(const value_reference &other) const noexcept requires enable_equal
			{
				return first == other.first || second == other.second;
			}
			[[nodiscard]] constexpr bool operator==(const value_type &other) const noexcept requires enable_equal
			{
				return first == other.first || second == other.second;
			}

			const key_type &first;
			std::conditional_t<Const, const mapped_type, mapped_type> &second;
		};
		template<bool Const>
		class value_pointer
		{
		public:
			using pointer = value_reference<Const> *;

		public:
			value_pointer(const value_pointer &) = delete;
			value_pointer &operator=(const value_pointer &) = delete;

			constexpr value_pointer(value_pointer &&) noexcept = default;
			constexpr value_pointer &operator=(value_pointer &&) noexcept = default;

			/* Conversions from relevant pointers. */
			constexpr value_pointer(value_type *ptr) noexcept : ref{*ptr} {}
			constexpr value_pointer(std::pair<key_type, mapped_type> *ptr) noexcept : ref{*ptr} {}
			constexpr value_pointer(const value_type *ptr) noexcept requires(Const) : ref{*ptr} {}
			constexpr value_pointer(const std::pair<key_type, mapped_type> *ptr) noexcept requires(Const) : ref{*ptr} {}

			/* Conversion from value reference. */
			constexpr value_pointer(value_reference<Const> &&ref) noexcept : ref{ref} {}
			constexpr value_pointer(const value_reference<Const> *ptr) noexcept : ref{*ptr} {}

			/* Pointer-like operators. */
			[[nodiscard]] constexpr pointer operator->() noexcept { return std::addressof(ref); }
			[[nodiscard]] constexpr value_reference<Const> operator*() const noexcept { return ref; }

			/* Since value_reference stores reference to adjacent pair members, comparing the first element is enough. */
			[[nodiscard]] constexpr auto operator<=>(const value_pointer &other) const noexcept
			{
				return std::addressof(ref.first) <=> std::addressof(other.ref.first);
			}
			[[nodiscard]] constexpr bool operator==(const value_pointer &other) const noexcept
			{
				return std::addressof(ref.first) == std::addressof(other.ref.first);
			}

		private:
			value_reference<Const> ref;
		};
		// clang-format on

		struct value_traits
		{
			using value_type = std::pair<const key_type, mapped_type>;

			using reference = value_reference<false>;
			using const_reference = value_reference<true>;

			using pointer = value_pointer<false>;
			using const_pointer = value_pointer<true>;
		};

		// clang-format off
		using table_type = detail::dense_hash_table<K, std::pair<key_type, mapped_type>, value_traits, KeyHash, KeyComp, pair_first, Alloc>;

		constexpr static bool transparent_key = requires
		{
			typename KeyHash::is_transparent;
			typename KeyComp::is_transparent;
		};
		// clang-format on

	public:
		typedef value_pointer<false> pointer;
		typedef value_pointer<true> const_pointer;
		typedef value_reference<false> reference;
		typedef value_reference<true> const_reference;
		typedef typename table_type::size_type size_type;
		typedef typename table_type::difference_type difference_type;

		typedef typename table_type::allocator_type allocator_type;
		typedef typename table_type::hash_type hash_type;
		typedef typename table_type::key_equal key_equal;

		typedef typename table_type::iterator iterator;
		typedef typename table_type::const_iterator const_iterator;
		typedef typename table_type::reverse_iterator reverse_iterator;
		typedef typename table_type::const_reverse_iterator const_reverse_iterator;
		typedef typename table_type::local_iterator local_iterator;
		typedef typename table_type::const_local_iterator const_local_iterator;

	public:
		constexpr dense_map() noexcept(std::is_nothrow_default_constructible_v<table_type>) = default;
		constexpr ~dense_map() = default;

		/** Constructs a map with the specified allocators.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr explicit dense_map(const allocator_type &alloc) : dense_map(key_equal{}, hash_type{}, alloc) {}
		/** Constructs a map with the specified hasher & allocators.
		 * @param key_hash Key hasher.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr explicit dense_map(const hash_type &key_hash, const allocator_type &alloc = allocator_type{})
			: dense_map(key_equal{}, key_hash, alloc)
		{
		}
		/** Constructs a map with the specified comparator, hasher & allocators.
		 * @param key_compare Key comparator.
		 * @param key_hash Key hasher.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr explicit dense_map(const key_equal &key_compare,
									 const hash_type &key_hash = {},
									 const allocator_type &alloc = allocator_type{})
			: m_table(key_compare, key_hash, alloc)
		{
		}
		/** Constructs a map with the specified minimum capacity.
		 * @param capacity Capacity of the map.
		 * @param key_compare Key comparator.
		 * @param key_hash Key hasher.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr explicit dense_map(size_type capacity,
									 const key_equal &key_compare = {},
									 const hash_type &key_hash = {},
									 const allocator_type &alloc = allocator_type{})
			: m_table(capacity, key_compare, key_hash, alloc)
		{
		}

		/** Constructs a map from a sequence of values.
		 * @param first Iterator to the start of the value sequence.
		 * @param first Iterator to the end of the value sequence.
		 * @param key_compare Key comparator.
		 * @param key_hash Key hasher.
		 * @param alloc Allocator used to allocate map's value array. */
		template<std::random_access_iterator Iterator>
		constexpr dense_map(Iterator first,
							Iterator last,
							const key_equal &key_compare = {},
							const hash_type &key_hash = {},
							const allocator_type &alloc = allocator_type{})
			: dense_map(static_cast<size_type>(std::distance(first, last)), key_compare, key_hash, alloc)
		{
			insert(first, last);
		}
		/** Constructs a map from a sequence of values.
		 * @param first Iterator to the start of the value sequence.
		 * @param first Iterator to the end of the value sequence.
		 * @param key_compare Key comparator.
		 * @param key_hash Key hasher.
		 * @param alloc Allocator used to allocate map's value array. */
		template<std::forward_iterator Iterator>
		constexpr dense_map(Iterator first,
							Iterator last,
							const key_equal &key_compare = {},
							const hash_type &key_hash = {},
							const allocator_type &alloc = allocator_type{})
			: dense_map(key_compare, key_hash, alloc)
		{
			insert(first, last);
		}
		/** Constructs a map from an initializer list.
		 * @param il Initializer list containing values.
		 * @param key_compare Key comparator.
		 * @param key_hash Key hasher.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr dense_map(std::initializer_list<value_type> il,
							const key_equal &key_compare = {},
							const hash_type &key_hash = {},
							const allocator_type &alloc = allocator_type{})
			: dense_map(il.begin(), il.end(), key_compare, key_hash, alloc)
		{
		}

		/** Copy-constructs the map. Allocator is copied via `select_on_container_copy_construction`.
		 * @param other Map to copy data and allocators from. */
		constexpr dense_map(const dense_map &other) noexcept(std::is_nothrow_copy_constructible_v<table_type>)
			: m_table(other.m_table)
		{
		}
		/** Copy-constructs the map.
		 * @param other Map to copy data and bucket allocator from.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr dense_map(const dense_map &other, const allocator_type &alloc) noexcept(
			std::is_nothrow_constructible_v<table_type, const table_type &, const allocator_type &>)
			: m_table(other.m_table, alloc)
		{
		}
		/** Move-constructs the map. Allocator is move-constructed.
		 * @param other Map to move elements and allocators from. */
		constexpr dense_map(dense_map &&other) noexcept(std::is_nothrow_move_constructible_v<table_type>)
			: m_table(std::move(other.m_table))
		{
		}
		/** Move-constructs the map.
		 * @param other Map to move elements and bucket allocator from.
		 * @param alloc Allocator used to allocate map's value array. */
		constexpr dense_map(dense_map &&other, const allocator_type &alloc) noexcept(
			std::is_nothrow_constructible_v<table_type, table_type &&, const allocator_type &>)
			: m_table(std::move(other.m_table), alloc)
		{
		}

		/** Copy-assigns the map.
		 * @param other Map to copy elements from. */
		constexpr dense_map &operator=(const dense_map &other)
		{
			if (this != &other) m_table = other.m_table;
			return *this;
		}
		/** Move-assigns the map.
		 * @param other Map to move elements from. */
		constexpr dense_map &operator=(dense_map &&other) noexcept(std::is_nothrow_move_assignable_v<table_type>)
		{
			m_table = std::move(other.m_table);
			return *this;
		}

		/** Returns iterator to the start of the map. */
		[[nodiscard]] constexpr iterator begin() noexcept { return m_table.begin(); }
		/** Returns iterator to the end of the map. */
		[[nodiscard]] constexpr iterator end() noexcept { return m_table.end(); }
		/** Returns const iterator to the start of the map. */
		[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_table.begin(); }
		/** Returns const iterator to the end of the map. */
		[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_table.end(); }
		/** @copydoc cbegin */
		[[nodiscard]] constexpr const_iterator begin() const noexcept { return cbegin(); }
		/** @copydoc cend */
		[[nodiscard]] constexpr const_iterator end() const noexcept { return cend(); }

		/** Returns reverse iterator to the end of the map. */
		[[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return m_table.rbegin(); }
		/** Returns reverse iterator to the start of the map. */
		[[nodiscard]] constexpr reverse_iterator rend() noexcept { return m_table.rend(); }
		/** Returns const reverse iterator to the end of the map. */
		[[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return m_table.crbegin(); }
		/** Returns const reverse iterator to the start of the map. */
		[[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return m_table.crend(); }
		/** @copydoc crbegin */
		[[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return crbegin(); }
		/** @copydoc crend */
		[[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return crend(); }

		/** Locates an element for the specific key.
		 * @param key Key to search for.
		 * @return Iterator to the element mapped to key. */
		constexpr auto find(const key_type &key) noexcept { return m_table.find(key); }
		/** @copydoc find */
		constexpr auto find(const key_type &key) const noexcept { return m_table.find(key); }
		/** @copydoc find
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		constexpr auto find(const auto &key) noexcept
			requires transparent_key
		{
			return m_table.find(key);
		}
		/** @copydoc find */
		constexpr auto find(const auto &key) const noexcept
			requires transparent_key
		{
			return m_table.find(key);
		}

		/** Checks if the map contains an element with specific key.
		 * @param key Key to search for. */
		constexpr bool contains(const key_type &key) const noexcept { return find(key) != end(); }
		/** @copydoc contains
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		constexpr bool contains(const auto &key) noexcept
			requires transparent_key
		{
			return find(key) != end();
		}

		/** Returns reference to object mapped to the specific key.
		 * @param key Key to search for.
		 * @return Reference to the object mapped to key.
		 * @throw std::out_of_range If the specified key is not present in the map. */
		constexpr mapped_type &at(const key_type &key)
		{
			if (auto iter = find(key); iter != end()) [[likely]]
				return iter->second;
			else
				throw std::out_of_range("Specified key is not present within the map");
		}
		/** Returns const reference to object mapped to the specific key.
		 * @param key Key to search for.
		 * @return Reference to the object mapped to key.
		 * @throw std::out_of_range If the specified key is not present in the map. */
		constexpr const mapped_type &at(const key_type &key) const
		{
			if (auto iter = find(key); iter != end()) [[likely]]
				return iter->second;
			else
				throw std::out_of_range("Specified key is not present within the map");
		}
		/** @copydoc at
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		constexpr mapped_type &at(const auto &key)
			requires transparent_key
		{
			if (auto iter = find(key); iter != end()) [[likely]]
				return iter->second;
			else
				throw std::out_of_range("Specified key is not present within the map");
		}
		/** @copydoc at */
		constexpr const mapped_type &at(const auto &key) const
			requires transparent_key
		{
			if (auto iter = find(key); iter != end()) [[likely]]
				return iter->second;
			else
				throw std::out_of_range("Specified key is not present within the map");
		}

		/** Returns reference to object at the specific key or inserts a new value if it does not exist.
		 * @param key Key to search for.
		 * @return Reference to the object mapped to key. */
		constexpr mapped_type &operator[](const key_type &key) { return try_emplace(key, mapped_type{}).first->second; }
		/** @copydoc operator[] */
		constexpr mapped_type &operator[](key_type &&key)
		{
			return try_emplace(std::forward<key_type>(key), mapped_type{}).first->second;
		}
		/** @copydoc operator[]
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		constexpr mapped_type &operator[](const auto &key)
			requires transparent_key
		{
			return try_emplace(key, mapped_type{}).first->second;
		}

		/** Empties the map's contents. */
		constexpr void clear() { m_table.clear(); }

		/** Re-hashes the map for the specified minimal capacity. */
		constexpr void rehash(size_type capacity) { m_table.rehash(capacity); }
		/** Resizes the internal storage to have space for at least n elements. */
		constexpr void reserve(size_type n) { m_table.reserve(n); }

		/** Attempts to construct a value in-place at the specified key.
		 * If such key is already associated with a value, does nothing.
		 * @param key Key for which to insert the value.
		 * @param args Arguments used to construct the mapped object.
		 * @return Pair where first element is the iterator to the potentially inserted element
		 * and second is boolean indicating whether the element was inserted (`true` if inserted, `false` otherwise). */
		template<typename... Args>
		constexpr std::pair<iterator, bool> try_emplace(key_type &&key, Args &&...args)
		{
			return m_table.try_emplace(std::forward<key_type>(key), std::forward<Args>(args)...);
		}
		/** @copydoc try_emplace */
		template<typename... Args>
		constexpr std::pair<iterator, bool> try_emplace(const key_type &key, Args &&...args)
		{
			return m_table.try_emplace(key, std::forward<Args>(args)...);
		}
		/** @copydoc try_emplace
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		template<typename... Args>
		constexpr std::pair<iterator, bool> try_emplace(const auto &key, Args &&...args)
			requires transparent_key
		{
			return m_table.try_emplace(key, std::forward<Args>(args)...);
		}
		/** Constructs a value (of value_type) in-place.
		 * If a value for the constructed key is already present within the map, replaces that value.
		 * @param args Arguments used to construct the value object.
		 * @return Pair where first element is the iterator to the inserted element
		 * and second is boolean indicating whether the element was inserted or replace (`true` if inserted new, `false` if replaced). */
		template<typename... Args>
		constexpr std::pair<iterator, bool> emplace(Args &&...args)
		{
			return m_table.emplace(std::forward<Args>(args)...);
		}

		/** Attempts to insert a value into the map.
		 * If a value with the same key is already present within the map, does not replace it.
		 * @param value Value to insert.
		 * @return Pair where first element is the iterator to the potentially inserted element
		 * and second is boolean indicating whether the element was inserted (`true` if inserted, `false` otherwise). */
		constexpr std::pair<iterator, bool> try_insert(value_type &&value)
		{
			return m_table.try_insert(std::forward<value_type>(value));
		}
		/** @copydoc try_insert */
		constexpr std::pair<iterator, bool> try_insert(const value_type &value) { return m_table.try_insert(value); }
		/** @copydetails try_insert
		 * @param hint Hint for where to insert the value.
		 * @param value Value to insert.
		 * @return Iterator to the potentially inserted element or the element that prevented insertion.
		 * @note Hint is required for compatibility with STL algorithms and is ignored.  */
		constexpr iterator try_insert([[maybe_unused]] const_iterator hint, value_type &&value)
		{
			return try_insert(std::forward<value_type>(value)).first;
		}
		/** @copydoc try_insert */
		constexpr iterator try_insert([[maybe_unused]] const_iterator hint, const value_type &value)
		{
			return try_insert(value).first;
		}
		/** Attempts to insert a sequence of values (of value_type) into the map.
		 * If values with the same key are already present within the map, does not replace them.
		 * @param first Iterator to the start of the value sequence.
		 * @param first Iterator to the end of the value sequence.
		 * @return Amount of elements inserted. */
		template<std::forward_iterator Iterator>
		constexpr size_type try_insert(Iterator first, Iterator last)
		{
			return m_table.try_insert(first, last);
		}
		/** Attempts to insert a sequence of values (of value_type) specified by the initializer list into the map.
		 * If values with the same key are already present within the map, does not replace them.
		 * @param il Initializer list containing the values.
		 * @return Amount of elements inserted. */
		constexpr size_type try_insert(std::initializer_list<value_type> il)
		{
			return try_insert(il.begin(), il.end());
		}

		/** Inserts a value (of value_type) into the map.
		 * If a value with the same key is already present within the map, replaces that value.
		 * @param value Value to insert.
		 * @return Pair where first element is the iterator to the inserted element
		 * and second is boolean indicating whether the element was inserted or replaced (`true` if inserted new, `false` if replaced). */
		constexpr std::pair<iterator, bool> insert(value_type &&value)
		{
			return m_table.insert(std::forward<value_type>(value));
		}
		/** @copydoc insert */
		constexpr std::pair<iterator, bool> insert(const value_type &value) { return m_table.insert(value); }
		/** @copydetails insert
		 * @param hint Hint for where to insert the value.
		 * @param value Value to insert.
		 * @return Iterator to the inserted element.
		 * @note Hint is required for compatibility with STL algorithms and is ignored. */
		constexpr iterator insert([[maybe_unused]] const_iterator hint, value_type &&value)
		{
			return insert(std::forward<value_type>(value)).first;
		}
		/** @copydoc insert */
		constexpr iterator insert([[maybe_unused]] const_iterator hint, const value_type &value)
		{
			return insert(value).first;
		}
		/** Inserts a sequence of values (of value_type) into the map.
		 * If values with the same key are already present within the map, replaces them.
		 * @param first Iterator to the start of the value sequence.
		 * @param first Iterator to the end of the value sequence.
		 * @return Amount of *new* elements inserted. */
		template<std::forward_iterator Iterator>
		constexpr size_type insert(Iterator first, Iterator last)
		{
			return m_table.insert(first, last);
		}
		/** Inserts a sequence of values (of value_type) specified by the initializer list into the map.
		 * If values with the same key are already present within the map, replaces them.
		 * @param il Initializer list containing the values.
		 * @return Amount of new elements inserted. */
		constexpr size_type insert(std::initializer_list<value_type> il) { return insert(il.begin(), il.end()); }

		/** Removes the specified element from the map.
		 * @param where Iterator to the target element.
		 * @return Iterator to the element after the erased one. */
		constexpr iterator erase(const_iterator where) { return m_table.erase(where); }
		/** Removes all elements in the [first, last) range.
		 * @param first Iterator to the first element of the target range.
		 * @param last Iterator to the last element of the target range.
		 * @return Iterator to the element after the erased sequence. */
		constexpr iterator erase(const_iterator first, const_iterator last) { return m_table.erase(first, last); }
		/** Removes element mapped to the specified key from the map if it is present.
		 * @param key Key of the target element.
		 * @return `true` if the element was removed, `false` otherwise. */
		constexpr bool erase(const key_type &key)
		{
			if (auto target = find(key); target != end())
			{
				erase(target);
				return true;
			}
			else
				return false;
		}
		/** @copydoc erase
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		constexpr bool erase(const auto &key)
			requires transparent_key
		{
			if (auto target = find(key); target != end())
			{
				erase(target);
				return true;
			}
			else
				return false;
		}

		/** Returns current amount of elements in the map. */
		[[nodiscard]] constexpr size_type size() const noexcept { return m_table.size(); }
		/** Returns current capacity of the map. */
		[[nodiscard]] constexpr size_type capacity() const noexcept { return m_table.capacity(); }
		/** Returns maximum possible amount of elements in the map. */
		[[nodiscard]] constexpr size_type max_size() const noexcept { return m_table.max_size(); }
		/** Checks if the map is empty. */
		[[nodiscard]] constexpr size_type empty() const noexcept { return size() == 0; }

		/** Returns current amount of buckets in the map. */
		[[nodiscard]] constexpr size_type bucket_count() const noexcept { return m_table.bucket_count(); }
		/** Returns the maximum amount of buckets. */
		[[nodiscard]] constexpr size_type max_bucket_count() const noexcept { return m_table.max_bucket_count(); }

		/** Returns local iterator to the start of a bucket. */
		[[nodiscard]] constexpr local_iterator begin(size_type bucket) noexcept { return m_table.begin(bucket); }
		/** Returns const local iterator to the start of a bucket. */
		[[nodiscard]] constexpr const_local_iterator cbegin(size_type bucket) const noexcept
		{
			return m_table.cbegin(bucket);
		}
		/** @copydoc cbegin */
		[[nodiscard]] constexpr const_local_iterator begin(size_type bucket) const noexcept
		{
			return m_table.begin(bucket);
		}
		/** Returns local iterator to the end of a bucket. */
		[[nodiscard]] constexpr local_iterator end(size_type bucket) noexcept { return m_table.end(bucket); }
		/** Returns const local iterator to the end of a bucket. */
		[[nodiscard]] constexpr const_local_iterator cend(size_type bucket) const noexcept
		{
			return m_table.cend(bucket);
		}
		/** @copydoc cbegin */
		[[nodiscard]] constexpr const_local_iterator end(size_type bucket) const noexcept
		{
			return m_table.end(bucket);
		}

		/** Returns the amount of elements stored within the bucket. */
		[[nodiscard]] constexpr size_type bucket_size(size_type bucket) const noexcept
		{
			return m_table.bucket_size(bucket);
		}
		/** Returns the index of the bucket associated with a key. */
		[[nodiscard]] constexpr size_type bucket(const key_type &key) const noexcept { return m_table.bucket(key); }
		/** @copydoc bucket
		 * @note This overload participates in overload resolution only
		 * if both key hasher and key comparator are transparent. */
		[[nodiscard]] constexpr size_type bucket(const auto &key) const noexcept
			requires transparent_key
		{
			return m_table.bucket(key);
		}
		/** Returns the index of the bucket containing the pointed-to element. */
		[[nodiscard]] constexpr size_type bucket(const_iterator iter) const noexcept { return m_table.bucket(iter); }

		/** Returns current load factor of the map. */
		[[nodiscard]] constexpr auto load_factor() const noexcept { return m_table.load_factor(); }
		/** Returns current max load factor of the map. */
		[[nodiscard]] constexpr auto max_load_factor() const noexcept { return m_table.max_load_factor; }
		/** Sets current max load factor of the map. */
		constexpr void max_load_factor(float f) noexcept
		{
			SEK_ASSERT(f > .0f);
			m_table.max_load_factor = f;
		}

		[[nodiscard]] constexpr allocator_type get_allocator() const noexcept { return m_table.allocator(); }

		[[nodiscard]] constexpr hash_type hash_function() const noexcept { return m_table.get_hash(); }
		[[nodiscard]] constexpr key_equal key_eq() const noexcept { return m_table.get_comp(); }

		[[nodiscard]] constexpr bool operator==(const dense_map &other) const noexcept
			requires(requires(const_iterator a, const_iterator b) { std::equal_to<>{}(*a, *b); })
		{
			return std::is_permutation(begin(), end(), other.begin(), other.end());
		}

		constexpr void swap(dense_map &other) noexcept { m_table.swap(other.m_table); }
		friend constexpr void swap(dense_map &a, dense_map &b) noexcept { a.swap(b); }

	private:
		/** Hash table used to implement the map. */
		table_type m_table;
	};
}	 // namespace sek