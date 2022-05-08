//
// Created by switchblade on 07/05/22.
//

#pragma once

#include <vector>

#include "assert.hpp"
#include "packed_pair.hpp"

namespace sek::detail
{
	/* Dense hash tables are implemented via a sparse array of bucket indices & a dense array of buckets,
	 * which form a closed addressing table.
	 * This allows for cache-efficient iteration over the table (iterators point to the dense array elements),
	 * as well as reduced memory overhead, since there are no empty buckets within the dense array.
	 *
	 * However, dense tables cannot provide iterator stability on erasure or insertion.
	 * This is due to the buckets being stored in the dense array by-value,
	 * thus on erasure buckets must be moved (or rather, the erased bucket is swapped with the last one),
	 * and on insertion the dense array may be re-allocated.
	 *
	 * The sparse array contains indices into the dense array of buckets, thus the buckets can be freely moved,
	 * as long as the sparse index is updated accordingly.
	 *
	 * To solve bucket contention, every bucket contains an offset into the dense array,
	 * where the next bucket in a chain is located. Since buckets are appended on top of the dense array,
	 * these offsets are stable.
	 *
	 * When a bucket is erased (and is swapped with the last bucket in the dense array), the offset links pointing
	 * to the erased and the swapped-with bucket are also updated. In order to do so, the swapped-with bucket's
	 * chain is traversed and is updated accordingly.
	 *
	 * In order for this to not affect performance, the default load factor is set to be below 1. */
	template<typename KeyType, typename ValueType, typename ValueTraits, typename KeyHash, typename KeyCompare, typename KeyExtract, typename Allocator>
	class dense_hash_table
	{
	public:
		typedef KeyType key_type;
		typedef ValueType value_type;
		typedef KeyCompare key_equal;
		typedef KeyHash hash_type;
		typedef value_type *pointer;
		typedef const value_type *const_pointer;
		typedef value_type &reference;
		typedef const value_type &const_reference;
		typedef std::size_t size_type;
		typedef std::ptrdiff_t difference_type;

	private:
		template<bool Const>
		using iterator_value = typename ValueTraits::template iterator_value<Const>;
		template<bool Const>
		using iterator_pointer = typename ValueTraits::template iterator_pointer<Const>;
		template<bool Const>
		using iterator_reference = typename ValueTraits::template iterator_reference<Const>;

		using sparse_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<size_type>;
		using sparse_data_t = std::vector<size_type, sparse_alloc>;

		constexpr static decltype(auto) key_get(const auto &v) { return KeyExtract{}(v); }

		constexpr static float initial_load_factor = .875f;
		constexpr static size_type initial_capacity = 8;
		constexpr static size_type npos = std::numeric_limits<size_type>::max();

		struct entry_type : ebo_base_helper<ValueType>
		{
			using ebo_base = ebo_base_helper<ValueType>;

			constexpr entry_type() = default;
			constexpr entry_type(const entry_type &) = default;
			constexpr entry_type &operator=(const entry_type &) = default;
			constexpr entry_type(entry_type &&) noexcept(std::is_nothrow_move_constructible_v<ebo_base>) = default;
			constexpr entry_type &operator=(entry_type &&) noexcept(std::is_nothrow_move_assignable_v<ebo_base>) = default;
			constexpr ~entry_type() = default;

			template<typename... Args>
			constexpr explicit entry_type(Args &&...args) : ebo_base(std::forward<Args>(args)...)
			{
			}

			[[nodiscard]] constexpr value_type &value() noexcept { return *ebo_base::get(); }
			[[nodiscard]] constexpr const value_type &value() const noexcept { return *ebo_base::get(); }
			[[nodiscard]] constexpr const key_type &key() const noexcept { return key_get(value()); }

			constexpr void swap(entry_type &other) noexcept(std::is_nothrow_swappable_v<ebo_base>)
			{
				using std::swap;
				ebo_base::swap(other);
				swap(next, other.next);
				swap(hash, other.hash);
			}

			/* Offset of the next bucket in the dense array. */
			size_type next = npos;
			/* Hash of the key. Cached by the bucket to avoid re-calculating hashes & allow for approximate comparison. */
			hash_t hash;
		};

		using dense_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<entry_type>;
		using dense_data_t = std::vector<entry_type, dense_alloc>;

		template<typename Iter>
		class dense_table_iterator
		{
			template<typename>
			friend class dense_table_iterator;

			friend class dense_hash_table;

			constexpr static auto is_const =
				std::is_const_v<std::remove_pointer_t<typename std::iterator_traits<Iter>::pointer>>;

		public:
			typedef iterator_value<is_const> value_type;
			typedef iterator_pointer<is_const> pointer;
			typedef iterator_reference<is_const> reference;
			typedef std::size_t size_type;
			typedef typename std::iterator_traits<Iter>::difference_type difference_type;
			typedef std::random_access_iterator_tag iterator_category;

		private:
			constexpr explicit dense_table_iterator(Iter i) noexcept : i(i) {}

		public:
			constexpr dense_table_iterator() noexcept = default;
			template<typename OtherIter, typename = std::enable_if_t<is_const && !dense_table_iterator<OtherIter>::is_const>>
			constexpr dense_table_iterator(const dense_table_iterator<OtherIter> &other) noexcept
				: dense_table_iterator(other.i)
			{
			}

			constexpr dense_table_iterator operator++(int) noexcept
			{
				auto temp = *this;
				++(*this);
				return temp;
			}
			constexpr dense_table_iterator &operator++() noexcept
			{
				++i;
				return *this;
			}
			constexpr dense_table_iterator &operator+=(difference_type n) noexcept
			{
				i += n;
				return *this;
			}
			constexpr dense_table_iterator operator--(int) noexcept
			{
				auto temp = *this;
				--(*this);
				return temp;
			}
			constexpr dense_table_iterator &operator--() noexcept
			{
				--i;
				return *this;
			}
			constexpr dense_table_iterator &operator-=(difference_type n) noexcept
			{
				i -= n;
				return *this;
			}

			constexpr dense_table_iterator operator+(difference_type n) const noexcept
			{
				return dense_table_iterator{i + n};
			}
			constexpr dense_table_iterator operator-(difference_type n) const noexcept
			{
				return dense_table_iterator{i - n};
			}
			constexpr difference_type operator-(const dense_table_iterator &other) const noexcept
			{
				return i - other.i;
			}

			/** Returns pointer to the target element. */
			[[nodiscard]] constexpr pointer get() const noexcept { return &i->value(); }
			/** @copydoc value */
			[[nodiscard]] constexpr pointer operator->() const noexcept { return get(); }

			/** Returns reference to the element at an offset. */
			[[nodiscard]] constexpr reference operator[](difference_type n) const noexcept { return i[n].value(); }
			/** Returns reference to the target element. */
			[[nodiscard]] constexpr reference operator*() const noexcept { return *get(); }

			[[nodiscard]] constexpr auto operator<=>(const dense_table_iterator &) const noexcept = default;
			[[nodiscard]] constexpr bool operator==(const dense_table_iterator &) const noexcept = default;

			friend constexpr void swap(dense_table_iterator &a, dense_table_iterator &b) noexcept
			{
				using std::swap;
				swap(a.i, b.i);
			}

		private:
			Iter i;
		};

		template<typename Iter>
		class dense_table_bucket_iterator
		{
			template<typename>
			friend class dense_table_bucket_iterator;

			friend class dense_hash_table;

			constexpr static auto is_const =
				std::is_const_v<std::remove_pointer_t<typename std::iterator_traits<Iter>::pointer>>;

		public:
			typedef iterator_value<is_const> value_type;
			typedef iterator_pointer<is_const> pointer;
			typedef iterator_reference<is_const> reference;
			typedef std::size_t size_type;
			typedef typename std::iterator_traits<Iter>::difference_type difference_type;
			typedef std::forward_iterator_tag iterator_category;

		private:
			constexpr explicit dense_table_bucket_iterator(Iter i, size_type off) noexcept : i(i), off(off) {}

		public:
			constexpr dense_table_bucket_iterator() noexcept = default;
			template<typename OtherIter, typename = std::enable_if_t<is_const && !dense_table_iterator<OtherIter>::is_const>>
			constexpr dense_table_bucket_iterator(const dense_table_iterator<OtherIter> &other) noexcept
				: dense_table_bucket_iterator(other.i, other.off)
			{
			}

			constexpr dense_table_bucket_iterator operator++(int) noexcept
			{
				auto temp = *this;
				++(*this);
				return temp;
			}
			constexpr dense_table_bucket_iterator &operator++() noexcept
			{
				off = i[off].next;
				return *this;
			}

			/** Returns pointer to the target element. */
			[[nodiscard]] constexpr pointer get() const noexcept { return &i->value(); }
			/** @copydoc value */
			[[nodiscard]] constexpr pointer operator->() const noexcept { return get(); }
			/** Returns reference to the target element. */
			[[nodiscard]] constexpr reference operator*() const noexcept { return *get(); }

			[[nodiscard]] constexpr bool operator==(const dense_table_bucket_iterator &) const noexcept = default;

			constexpr void swap(dense_table_bucket_iterator &other) noexcept
			{
				using std::swap;
				swap(i, other.i);
				swap(off, other.off);
			}
			friend constexpr void swap(dense_table_bucket_iterator &a, dense_table_bucket_iterator &b) noexcept
			{
				a.swap(b);
			}

		private:
			Iter i;
			size_type off;
		};

	public:
		typedef dense_table_iterator<typename dense_data_t::iterator> iterator;
		typedef dense_table_iterator<typename dense_data_t::const_iterator> const_iterator;
		typedef std::reverse_iterator<iterator> reverse_iterator;
		typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
		typedef dense_table_bucket_iterator<typename dense_data_t::iterator> local_iterator;
		typedef dense_table_bucket_iterator<typename dense_data_t::const_iterator> const_local_iterator;

		typedef dense_alloc value_allocator_type;
		typedef sparse_alloc bucket_allocator_type;

	private:
		[[nodiscard]] constexpr static entry_type &to_entry(iterator it) noexcept { return *std::to_address(it.i); }
		[[nodiscard]] constexpr static const entry_type &to_entry(const_iterator it) noexcept
		{
			return *std::to_address(it.i);
		}

	public:
		constexpr dense_hash_table() : sparse_data{initial_capacity, key_equal{}} {}
		constexpr dense_hash_table(const dense_hash_table &) = default;
		constexpr dense_hash_table &operator=(const dense_hash_table &) = default;
		constexpr dense_hash_table(dense_hash_table &&) = default;
		constexpr dense_hash_table &operator=(dense_hash_table &&) = default;
		constexpr ~dense_hash_table() = default;

		constexpr dense_hash_table(const key_equal &equal,
								   const hash_type &hash,
								   const value_allocator_type &value_alloc,
								   const bucket_allocator_type &bucket_alloc)
			: dense_hash_table{initial_capacity, equal, hash, value_alloc, bucket_alloc}
		{
		}
		constexpr dense_hash_table(size_type bucket_count,
								   const key_equal &equal,
								   const hash_type &hash,
								   const value_allocator_type &value_alloc,
								   const bucket_allocator_type &bucket_alloc)
			: dense_data{value_alloc, equal}, sparse_data{bucket_alloc, hash}
		{
			rehash(bucket_count);
		}
		constexpr dense_hash_table(const dense_hash_table &other, const value_allocator_type &value_alloc)
			: dense_data{std::piecewise_construct,
						 std::forward_as_tuple(other.value_vector(), value_alloc),
						 std::forward_as_tuple(other.dense_data.second())},
			  sparse_data{other.sparse_data},
			  max_load_factor{other.max_load_factor}
		{
		}
		constexpr dense_hash_table(const dense_hash_table &other,
								   const value_allocator_type &value_alloc,
								   const bucket_allocator_type &bucket_alloc)
			: dense_data{std::piecewise_construct,
						 std::forward_as_tuple(other.value_vector(), value_alloc),
						 std::forward_as_tuple(other.dense_data.second())},
			  sparse_data{std::piecewise_construct,
						  std::forward_as_tuple(other.bucket_vector(), bucket_alloc),
						  std::forward_as_tuple(other.sparse_data.second())},
			  max_load_factor{other.max_load_factor}
		{
		}

		constexpr dense_hash_table(dense_hash_table &&other, const value_allocator_type &value_alloc)
			: dense_data{std::piecewise_construct,
						 std::forward_as_tuple(std::move(other.value_vector()), value_alloc),
						 std::forward_as_tuple(std::move(other.dense_data.second()))},
			  sparse_data{std::move(other.sparse_data)},
			  max_load_factor{other.max_load_factor}
		{
		}
		constexpr dense_hash_table(dense_hash_table &&other,
								   const value_allocator_type &value_alloc,
								   const bucket_allocator_type &bucket_alloc)
			: dense_data{std::piecewise_construct,
						 std::forward_as_tuple(std::move(other.value_vector()), value_alloc),
						 std::forward_as_tuple(std::move(other.dense_data.second()))},
			  sparse_data{std::piecewise_construct,
						  std::forward_as_tuple(std::move(other.bucket_vector()), bucket_alloc),
						  std::forward_as_tuple(std::move(other.sparse_data.second()))},
			  max_load_factor{other.max_load_factor}
		{
		}

		[[nodiscard]] constexpr iterator begin() noexcept { return iterator{value_vector().begin()}; }
		[[nodiscard]] constexpr const_iterator cbegin() const noexcept
		{
			return const_iterator{value_vector().cbegin()};
		}
		[[nodiscard]] constexpr const_iterator begin() const noexcept { return cbegin(); }

		[[nodiscard]] constexpr iterator end() noexcept { return iterator{value_vector().end()}; }
		[[nodiscard]] constexpr const_iterator cend() const noexcept { return const_iterator{value_vector().cend()}; }
		[[nodiscard]] constexpr const_iterator end() const noexcept { return cend(); }

		[[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator{begin()}; }
		[[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept
		{
			return const_reverse_iterator{cbegin()};
		}
		[[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return crbegin(); }

		[[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator{end()}; }
		[[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator{cend()}; }
		[[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return crend(); }

		[[nodiscard]] constexpr size_type size() const noexcept { return value_vector().size(); }
		[[nodiscard]] constexpr size_type capacity() const noexcept
		{
			/* Capacity needs to take into account the max load factor. */
			return static_cast<size_type>(static_cast<float>(bucket_count()) * max_load_factor);
		}
		[[nodiscard]] constexpr size_type max_size() const noexcept
		{
			/* Max size cannot exceed max load factor of max capacity. */
			return static_cast<size_type>(static_cast<float>(std::numeric_limits<size_type>::max()) * max_load_factor);
		}
		[[nodiscard]] constexpr float load_factor() const noexcept
		{
			return static_cast<float>(size()) / static_cast<float>(bucket_count());
		}

		[[nodiscard]] constexpr size_type bucket_count() const noexcept { return bucket_vector().size(); }
		[[nodiscard]] constexpr size_type max_bucket_count() const noexcept { return bucket_vector().max_size(); }

		[[nodiscard]] constexpr local_iterator begin(size_type bucket) noexcept
		{
			return local_iterator{value_vector().begin(), bucket};
		}
		[[nodiscard]] constexpr const_local_iterator cbegin(size_type bucket) const noexcept
		{
			return const_local_iterator{value_vector().cbegin(), bucket};
		}
		[[nodiscard]] constexpr const_local_iterator begin(size_type bucket) const noexcept { return cbegin(bucket); }

		[[nodiscard]] constexpr local_iterator end(size_type) noexcept
		{
			return local_iterator{value_vector().begin(), npos};
		}
		[[nodiscard]] constexpr const_local_iterator cend(size_type) const noexcept
		{
			return const_local_iterator{value_vector().cbegin(), npos};
		}
		[[nodiscard]] constexpr const_local_iterator end(size_type bucket) const noexcept { return cend(bucket); }

		[[nodiscard]] constexpr size_type bucket_size(size_type bucket) const noexcept
		{
			return static_cast<size_type>(std::distance(begin(bucket), end(bucket)));
		}
		[[nodiscard]] constexpr size_type bucket(const key_type &key) const noexcept
		{
			return get_chain(key_hash(key));
		}
		[[nodiscard]] constexpr size_type bucket(const_iterator iter) const noexcept { return get_chain(iter.i->hash); }

		[[nodiscard]] constexpr iterator find(const key_type &key) noexcept
		{
			return begin() + static_cast<difference_type>(find_impl(key_hash(key), key));
		}
		[[nodiscard]] constexpr const_iterator find(const key_type &key) const noexcept
		{
			return cbegin() + static_cast<difference_type>(find_impl(key_hash(key), key));
		}

		constexpr void clear()
		{
			bucket_vector().clear();
			value_vector().clear();
		}

		constexpr void rehash(size_type new_cap)
		{
			/* Adjust the capacity to be at least large enough to fit the current size. */
			new_cap = math::max(static_cast<size_type>(static_cast<float>(size()) / max_load_factor), new_cap, initial_capacity);

			/* Don't do anything if the capacity did not change after the adjustment. */
			if (new_cap != bucket_vector().capacity()) [[likely]]
				rehash_impl(new_cap);
		}
		constexpr void reserve(size_type n)
		{
			value_vector().reserve(n);
			rehash(static_cast<size_type>(static_cast<float>(n) / max_load_factor));
		}

		template<typename... Args>
		constexpr std::pair<iterator, bool> emplace(Args &&...args)
			requires std::constructible_from<value_type, Args...>
		{
			maybe_rehash();

			/* Temporary entry needs to be created at first. */
			auto &entry = value_vector().emplace_back(std::forward<Args>(args)...);
			const auto h = entry.hash = key_hash(entry.key());
			auto &chain_idx = get_chain(h);
			while (chain_idx != npos)
				if (auto &candidate = value_vector()[chain_idx]; candidate.hash == h && key_comp(entry.key(), candidate.key()))
				{
					/* Found a candidate for replacing. Move-assign or swap it from the temporary back entry. */
					if constexpr (std::is_move_assignable_v<entry_type>)
						candidate = std::move(value_vector().back());
					else
					{
						using std::swap;
						swap(candidate, value_vector().back());
					}
					value_vector().pop_back(); /* Pop the temporary. */
					return {begin() + static_cast<difference_type>(chain_idx), false};
				}

			/* No suitable entry for replacing was found, add new link. */
			return {begin() + static_cast<difference_type>(chain_idx = size() - 1), true};
		}
		template<typename... Args>
		constexpr std::pair<iterator, bool> try_emplace(const key_type &key, Args &&...args)
		{
			// clang-format off
			return try_insert_impl(key, std::piecewise_construct,
								   std::forward_as_tuple(key),
								   std::forward_as_tuple(std::forward<Args>(args)...));
			// clang-format on
		}
		template<typename... Args>
		constexpr std::pair<iterator, bool> try_emplace(key_type &&key, Args &&...args)
		{
			// clang-format off
			return try_insert_impl(key, std::piecewise_construct,
								   std::forward_as_tuple(std::forward<key_type>(key)),
								   std::forward_as_tuple(std::forward<Args>(args)...));
			// clang-format on
		}

		template<std::forward_iterator Iter>
		constexpr size_type insert(Iter first, Iter last)
		{
			size_type inserted = 0;
			while (first != last) inserted += insert(*first++).second;
			return inserted;
		}
		constexpr std::pair<iterator, bool> insert(const value_type &value)
		{
			return insert_impl(key_get(value), value);
		}
		constexpr std::pair<iterator, bool> insert(value_type &&value)
		{
			return insert_impl(key_get(value), std::forward<value_type>(value));
		}

		template<std::forward_iterator Iter>
		constexpr size_type try_insert(Iter first, Iter last)
		{
			size_type inserted = 0;
			while (first != last) inserted += try_insert(*first++).second;
			return inserted;
		}
		constexpr std::pair<iterator, bool> try_insert(const value_type &value)
		{
			return try_insert_impl(key_get(value), value);
		}
		constexpr std::pair<iterator, bool> try_insert(value_type &&value)
		{
			return try_insert_impl(key_get(value), std::forward<value_type>(value));
		}

		constexpr iterator erase(const_iterator first, const_iterator last)
		{
			/* Iterate backwards here, since iterators after the erased one can be invalidated. */
			iterator result = end();
			while (first < last) result = erase(--last);
			return result;
		}
		constexpr iterator erase(const_iterator where)
		{
			return erase_impl(to_entry(where).hash, key_get(*where.get()));
		}
		constexpr iterator erase(const key_type &key) { return erase_impl(key_hash(key), key); }

		[[nodiscard]] constexpr auto value_allocator() const noexcept { return value_vector().get_allocator(); }
		[[nodiscard]] constexpr auto bucket_allocator() const noexcept { return bucket_vector().get_allocator(); }
		[[nodiscard]] constexpr auto &get_hash() const noexcept { return sparse_data.second(); }
		[[nodiscard]] constexpr auto &get_comp() const noexcept { return dense_data.second(); }

		constexpr void swap(dense_hash_table &other) noexcept
		{
			using std::swap;
			swap(sparse_data, other.sparse_data);
			swap(dense_data, other.dense_data);
			swap(max_load_factor, other.max_load_factor);
		}

	private:
		[[nodiscard]] constexpr auto &value_vector() noexcept { return dense_data.first(); }
		[[nodiscard]] constexpr const auto &value_vector() const noexcept { return dense_data.first(); }
		[[nodiscard]] constexpr auto &bucket_vector() noexcept { return sparse_data.first(); }
		[[nodiscard]] constexpr const auto &bucket_vector() const noexcept { return sparse_data.first(); }

		[[nodiscard]] constexpr auto key_hash(const key_type &k) const { return sparse_data.second()(k); }
		[[nodiscard]] constexpr auto key_comp(const key_type &a, const key_type &b) const
		{
			return dense_data.second()(a, b);
		}
		[[nodiscard]] constexpr auto &get_chain(hash_t h) noexcept
		{
			return bucket_vector()[h % bucket_vector().size()];
		}
		[[nodiscard]] constexpr const auto &get_chain(hash_t h) const noexcept
		{
			return bucket_vector()[h % bucket_vector().size()];
		}

		[[nodiscard]] constexpr size_type find_impl(hash_t h, const key_type &key) const noexcept
		{
			for (auto idx = get_chain(h); idx != npos;)
				if (auto &entry = value_vector()[idx]; entry.hash == h && key_comp(key, entry.key()))
					return idx;
				else
					idx = entry.next;
			return value_vector().size();
		}

		template<typename T>
		[[nodiscard]] constexpr std::pair<iterator, bool> insert_impl(const key_type &key, T &&value) noexcept
		{
			maybe_rehash();

			/* See if we can replace any entry. */
			const auto h = key_hash(key);
			auto &chain_idx = get_chain(h);
			while (chain_idx != npos)
				if (auto &candidate = value_vector()[chain_idx]; candidate.hash == h && key_comp(key, candidate.key()))
				{
					/* Found a candidate for replacing, replace the value & hash. */
					if constexpr (requires { candidate.value() = std::forward<T>(value); })
						candidate.value() = std::forward<T>(value);
					else
					{
						std::destroy_at(&candidate);
						std::construct_at(&candidate, std::forward<T>(value));
					}
					candidate.hash = h;
					return {begin() + static_cast<difference_type>(chain_idx), false};
				}

			/* No candidate for replacing found, create new entry. */
			value_vector().emplace_back(std::forward<T>(value)).hash = h;
			return {begin() + static_cast<difference_type>(chain_idx = size() - 1), true};
		}
		template<typename... Args>
		[[nodiscard]] constexpr std::pair<iterator, bool> try_insert_impl(const key_type &key, Args &&...args) noexcept
		{
			maybe_rehash();

			/* See if an entry already exists. */
			const auto h = key_hash(key);
			auto &chain_idx = get_chain(h);
			while (chain_idx != npos)
			{
				if (auto &existing = value_vector()[chain_idx]; existing.hash == h && key_comp(key, existing.key()))
					return {begin() + static_cast<difference_type>(chain_idx), false};
			}

			/* No existing entry found, create new entry. */
			value_vector().emplace_back(std::forward<Args>(args)...).hash = h;
			return {begin() + static_cast<difference_type>(chain_idx = size() - 1), true};
		}

		constexpr void maybe_rehash()
		{
			if (size() >= static_cast<size_type>(static_cast<float>(bucket_count()) * max_load_factor)) [[unlikely]]
				rehash_impl(bucket_count() * 2);
		}
		constexpr void rehash_impl(size_type new_cap)
		{
			/* Clear & resize the vector filled with npos. */
			bucket_vector().clear();
			bucket_vector().resize(new_cap, npos);

			/* Go through each entry & re-insert it. */
			for (std::size_t i = 0; i < value_vector().size(); ++i)
			{
				auto &entry = value_vector()[i];
				auto &chain_idx = get_chain(entry.hash);

				/* Will also handle cases where chain_idx is npos (empty chain). */
				entry.next = chain_idx;
				chain_idx = i;
			}
		}

		constexpr iterator erase_impl(hash_t h, const key_type &key)
		{
			/* Remove the entry from it's chain. */
			for (auto &chain_idx = get_chain(h); chain_idx != npos;)
			{
				const auto pos = chain_idx;
				auto entry_iter = value_vector().begin() + static_cast<difference_type>(pos);

				/* Un-link the entry from the chain & swap with the last entry. */
				chain_idx = entry_iter->next;
				if (entry_iter->hash == h && key_comp(key, entry_iter->key()))
				{
					if constexpr (std::is_move_assignable_v<entry_type>)
						*entry_iter = std::move(value_vector().back());
					else
						entry_iter->swap(value_vector().back());

					/* Find the chain offset pointing to the old position & replace it with the new position. */
					value_vector().pop_back();
					for (chain_idx = get_chain(entry_iter->hash); chain_idx != npos;)
						if (chain_idx == size()) /* Since we popped the back entry, size will be the index of the old back. */
						{
							chain_idx = pos;
							break;
						}
					return iterator{entry_iter};
				}
			}
			return end();
		}

		packed_pair<dense_data_t, key_equal> dense_data;
		packed_pair<sparse_data_t, hash_type> sparse_data;

	public:
		float max_load_factor = initial_load_factor;
	};
}	 // namespace sek::detail