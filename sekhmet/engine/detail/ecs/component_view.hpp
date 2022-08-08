/*
 * Created by switchblade on 31/07/22
 */

#pragma once

#include "component_set.hpp"

namespace sek::engine
{
	template<typename, typename = excluded_t<>, typename = optional_t<>>
	class component_view;

	/** @brief Structure used to provide a simple view of components for a set of entities.
	 * @tparam I Component types captured by the view.
	 * @tparam E Component types excluded from the view.
	 * @tparam O Optional components of the view. */
	template<typename... I, typename... E, typename... O>
	class component_view<included_t<I...>, excluded_t<E...>, optional_t<O...>>
	{
		static_assert(sizeof...(I) != 0, "View include at least 1 component type");

		using common_set = basic_component_set;

		template<typename T>
		using set_ptr_t = transfer_cv_t<T, component_set<std::remove_cv_t<T>>> *;
		using inc_ptr = std::tuple<set_ptr_t<I>...>;
		using exc_ptr = std::tuple<set_ptr_t<E>...>;
		using opt_ptr = std::tuple<set_ptr_t<O>...>;

		template<typename T>
		constexpr static bool is_inc = is_in_v<std::remove_cv_t<T>, std::remove_cv_t<I>...>;
		template<typename T>
		constexpr static bool is_opt = is_in_v<std::remove_cv_t<T>, std::remove_cv_t<O>...>;

		template<typename T>
		[[nodiscard]] constexpr static bool accept(entity_t e, const inc_ptr &inc) noexcept
		{
			return std::get<set_ptr_t<T>>(inc)->contains(e);
		}
		template<typename T>
		[[nodiscard]] constexpr static bool reject(entity_t e, const exc_ptr &exc) noexcept
		{
			return std::get<set_ptr_t<T>>(exc)->contains(e);
		}

		class view_iterator
		{
			friend class component_view;

		public:
			typedef entity_t value_type;
			typedef const entity_t &reference;
			typedef const entity_t *pointer;
			typedef std::size_t size_type;
			typedef std::ptrdiff_t difference_type;
			typedef std::bidirectional_iterator_tag iterator_category;

		private:
			constexpr explicit view_iterator(const component_view *view) noexcept : m_view(view) {}
			constexpr view_iterator(const component_view *view, difference_type off) noexcept
				: m_view(view), m_off(next_valid(off))
			{
			}
			constexpr view_iterator(const component_view *view, size_type off) noexcept
				: view_iterator(view, static_cast<difference_type>(off))
			{
			}

		public:
			constexpr view_iterator() noexcept = default;

			constexpr view_iterator operator++(int) noexcept
			{
				auto temp = *this;
				++(*this);
				return temp;
			}
			constexpr view_iterator &operator++() noexcept
			{
				m_off = next_valid(m_off - 1);
				return *this;
			}
			constexpr view_iterator operator--(int) noexcept
			{
				auto temp = *this;
				--(*this);
				return temp;
			}
			constexpr view_iterator &operator--() noexcept
			{
				for (const auto end = m_view->size_hint(); m_off != end && !valid(m_off);) ++m_off;
				return *this;
			}

			/** Returns pointer to the target entity. */
			[[nodiscard]] constexpr pointer get() const noexcept { return get(m_off); }
			/** @copydoc get */
			[[nodiscard]] constexpr pointer operator->() const noexcept { return get(); }
			/** Returns reference to the target entity */
			[[nodiscard]] constexpr reference operator*() const noexcept { return *get(); }

			[[nodiscard]] constexpr auto operator<=>(const view_iterator &other) const noexcept
			{
				return m_off <=> other.m_off;
			}
			[[nodiscard]] constexpr bool operator==(const view_iterator &other) const noexcept
			{
				return m_off == other.m_off;
			}

			constexpr void swap(view_iterator &other) noexcept
			{
				std::swap(m_view, other.m_view);
				std::swap(m_off, other.m_off);
			}
			friend constexpr void swap(view_iterator &a, view_iterator &b) noexcept { a.swap(b); }

		private:
			[[nodiscard]] constexpr difference_type next_valid(difference_type i) const noexcept
			{
				while (i != 0 && !valid(i)) --i;
				return i;
			}
			[[nodiscard]] constexpr pointer get(difference_type i) const noexcept
			{
				return m_view->m_set->data() + i - 1;
			}
			[[nodiscard]] constexpr bool valid(difference_type i) const noexcept { return m_view->contains(*get(i)); }

			const component_view *m_view = nullptr;
			difference_type m_off = 0;
		};

	public:
		typedef typename view_iterator::value_type value_type;
		typedef typename view_iterator::pointer pointer;
		typedef typename view_iterator::pointer const_pointer;
		typedef typename view_iterator::reference reference;
		typedef typename view_iterator::reference const_reference;
		typedef view_iterator iterator;
		typedef view_iterator const_iterator;
		typedef std::reverse_iterator<view_iterator> reverse_iterator;
		typedef std::reverse_iterator<view_iterator> const_reverse_iterator;
		typedef typename view_iterator::size_type size_type;
		typedef typename view_iterator::difference_type difference_type;

	private:
		template<typename T>
		[[nodiscard]] constexpr static const common_set *select_common(T *storage) noexcept
		{
			return static_cast<const common_set *>(storage);
		}
		template<typename T0, typename T1, typename... Ts>
		[[nodiscard]] constexpr static const common_set *select_common(T0 *a, T1 *b, Ts *...rest) noexcept
		{
			return a->size() < b->size() ? select_common(a, rest...) : select_common(b, rest...);
		}

	public:
		/** Initializes an empty view. */
		constexpr component_view() noexcept = default;

		constexpr component_view(const component_view &) = default;
		constexpr component_view &operator=(const component_view &) = default;
		constexpr component_view(component_view &&) noexcept = default;
		constexpr component_view &operator=(component_view &&) noexcept = default;

		/** Initializes component view from pointers to component sets.
		 * @param inc Pointers to component sets of included components.
		 * @param exc Pointers to component sets of excluded components.
		 * @param opt Pointers to component sets of optional components.
		 * @note The smallest component set will be used as the main set. */
		constexpr explicit component_view(set_ptr_t<I>... inc, set_ptr_t<E>... exc, set_ptr_t<O>... opt)
			: m_set(select_common(inc...)), m_included(inc...), m_excluded(exc...), m_optional(opt...)
		{
			SEK_ASSERT(((inc != nullptr) && ...), "Included component sets can not be null");
		}

		/** Rebinds view to use the specified component set as the main set. */
		template<typename C>
		constexpr component_view &rebind() noexcept
		{
			static_assert(is_in_v<std::remove_cv_t<C>, std::remove_cv_t<I>...>, "Can only rebind included component sets");
			m_set = std::get<set_ptr_t<C>>(m_included);
			return *this;
		}

		/** Returns iterator to the first entity. */
		[[nodiscard]] constexpr iterator begin() const noexcept { return iterator{this, size_hint()}; }
		/** @copydoc begin */
		[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
		/** Returns iterator one past the last entity. */
		[[nodiscard]] constexpr iterator end() const noexcept { return iterator{this}; }
		/** @copydoc end */
		[[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
		/** Returns reverse iterator to the last entity. */
		[[nodiscard]] constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator{end()}; }
		/** @copydoc rbegin */
		[[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
		/** Returns reverse iterator one past the first entity. */
		[[nodiscard]] constexpr reverse_iterator rend() const noexcept { return reverse_iterator{begin()}; }
		/** @copydoc rend */
		[[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

		/** Returns reference to the first entity. */
		[[nodiscard]] constexpr reference front() const noexcept { return *begin(); }
		/** Returns reference to the last entity. */
		[[nodiscard]] constexpr reference back() const noexcept { return *std::prev(end()); }
		/** Checks if the the view is empty. */
		[[nodiscard]] constexpr bool empty() const noexcept { return begin() == end(); }

		/** Returns the size of the of the main set (approximate size of the view). */
		[[nodiscard]] constexpr size_type size_hint() const noexcept { return m_set != nullptr ? m_set->size() : 0; }

		/** Checks if the the view contains the specified entity. */
		[[nodiscard]] constexpr bool contains(entity_t entity) const noexcept
		{
			const auto &inc = m_included;
			const auto &exc = m_excluded;
			return m_set != nullptr && (accept<I>(entity, inc) && ...) && !(reject<E>(entity, exc) || ...);
		}
		/** Returns iterator to the specified entity, or an end iterator if the entity does not belong to the view. */
		[[nodiscard]] constexpr iterator find(entity_t entity) const noexcept
		{
			if (contains(entity)) [[likely]]
				return iterator{this, m_set->offset(entity) + 1};
			else
				return end();
		}

		/** Returns pointers to components associated with the entity pointed to by the iterator.
		 *
		 * @param which Iterator to the entity to get components for.
		 * @return Tuple of pointers to entity components, or a single pointer if only one component is specified.
		 * @note Pointers to optional components may be null. */
		template<typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get(const_iterator which) const noexcept
		{
			return get_impl<Ts...>(*which);
		}
		/** Returns pointers to components associated with the specified entity.
		 *
		 * @param entity Entity to get components for.
		 * @return Tuple of pointers to entity components, or a single pointer if only one component is specified.
		 *
		 * @note Pointers to optional components may be null.
		 * @warning Using an entity not belonging to the view results in undefined behavior. */
		template<typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get(entity_t entity) const noexcept
		{
			return get_impl<Ts...>(entity);
		}

		/** Applies the functor to every entity of the view.
		 * Functor may optionally return a value, which if evaluated to `false`, prematurely terminates iteration. */
		template<std::invocable<entity_t, I *..., O *...> F>
		constexpr void for_each(F &&f) const
		{
			for (auto first = begin(), last = end(); first != last; ++first)
			{
				const auto cmp = std::tuple<I *..., O *...>{get<I..., O...>(first)};
				const auto e = *first;

				if constexpr (std::convertible_to<std::invoke_result_t<F, entity_t, I *..., O *...>, bool>)
				{
					if (!std::invoke(std::forward<F>(f), e, std::get<I *>(cmp)..., std::get<O *>(cmp)...)) [[unlikely]]
						break;
				}
				else
					std::invoke(std::forward<F>(f), e, std::get<I *>(cmp)..., std::get<O *>(cmp)...);
			}
		}

		constexpr void swap(component_view &other) noexcept
		{
			using std::swap;
			swap(m_set, other.m_set);
			swap(m_included, other.m_included);
			swap(m_excluded, other.m_excluded);
			swap(m_optional, other.m_optional);
		}
		friend constexpr void swap(component_view &a, component_view &b) noexcept { a.swap(b); }

	private:
		template<typename T>
		[[nodiscard]] constexpr auto *get_included() const noexcept
		{
			return std::get<set_ptr_t<T>>(m_included);
		}
		template<typename T>
		[[nodiscard]] constexpr auto *get_optional() const noexcept
		{
			return std::get<set_ptr_t<T>>(m_optional);
		}

		template<typename T, typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get_impl(entity_t e) const noexcept
		{
			if constexpr (sizeof...(Ts) != 0)
				return std::tuple<T *, Ts *...>{get_impl<T>(e), get_impl<Ts>(e)...};
			else if constexpr (is_inc<T>)
				return static_cast<T *>(std::addressof(get_included<T>()->get(e)));
			else
			{
				const auto set = get_optional<T>();
				if (set == nullptr) [[unlikely]]
					return static_cast<T *>(nullptr);
				const auto pos = set->find(e);
				if (pos == set->end()) [[unlikely]]
					return static_cast<T *>(nullptr);
				return static_cast<T *>(std::addressof(pos->second));
			}
		}

		const common_set *m_set;
		inc_ptr m_included;
		exc_ptr m_excluded;
		opt_ptr m_optional;
	};

	template<typename I, typename... O>
	class component_view<included_t<I>, excluded_t<>, optional_t<O...>>
	{
		using common_set = basic_component_set;

		template<typename T>
		using set_t = transfer_cv_t<T, component_set<std::remove_cv_t<T>>>;
		template<typename T>
		using set_ptr_t = set_t<T> *;

		using opt_ptr = std::tuple<set_ptr_t<O>...>;

		template<typename T>
		constexpr static bool is_inc = std::same_as<std::remove_cv_t<T>, std::remove_cv_t<I>>;
		template<typename T>
		constexpr static bool is_opt = is_in_v<std::remove_cv_t<T>, std::remove_cv_t<O>...>;

		class view_iterator
		{
			friend class component_view;

			using iter_t = typename set_t<I>::iterator;

		public:
			typedef entity_t value_type;
			typedef const entity_t &reference;
			typedef const entity_t *pointer;
			typedef typename iter_t::size_type size_type;
			typedef typename iter_t::difference_type difference_type;
			typedef typename iter_t::iterator_category iterator_category;

		private:
			constexpr explicit view_iterator(iter_t iter) noexcept : m_iter(iter) {}

		public:
			constexpr view_iterator() noexcept = default;

			constexpr view_iterator operator++(int) noexcept
			{
				auto temp = *this;
				++(*this);
				return temp;
			}
			constexpr view_iterator &operator++() noexcept
			{
				++m_iter;
				return *this;
			}
			constexpr view_iterator &operator+=(difference_type n) noexcept
			{
				m_iter += n;
				return *this;
			}
			constexpr view_iterator operator--(int) noexcept
			{
				auto temp = *this;
				--(*this);
				return temp;
			}
			constexpr view_iterator &operator--() noexcept
			{
				--m_iter;
				return *this;
			}
			constexpr view_iterator &operator-=(difference_type n) noexcept
			{
				m_iter -= n;
				return *this;
			}

			[[nodiscard]] constexpr view_iterator operator+(difference_type n) const noexcept
			{
				return view_iterator{m_iter + n};
			}
			[[nodiscard]] constexpr view_iterator operator-(difference_type n) const noexcept
			{
				return view_iterator{m_iter - n};
			}
			[[nodiscard]] constexpr difference_type operator-(const view_iterator &other) const noexcept
			{
				return m_iter - other.m_iter;
			}

			/** Returns pointer to the target entity. */
			[[nodiscard]] constexpr pointer get() const noexcept { return std::addressof(m_iter->first); }
			/** @copydoc get */
			[[nodiscard]] constexpr pointer operator->() const noexcept { return get(); }
			/** Returns reference to the target entity */
			[[nodiscard]] constexpr reference operator*() const noexcept { return *get(); }
			/** Returns reference to the entity at offset `i` from this iterator. */
			[[nodiscard]] constexpr reference operator[](difference_type i) const noexcept { return m_iter[i].first; }

			[[nodiscard]] constexpr auto operator<=>(const view_iterator &) const noexcept = default;
			[[nodiscard]] constexpr bool operator==(const view_iterator &) const noexcept = default;

			constexpr void swap(view_iterator &other) noexcept { swap(m_iter, other.m_iter); }
			friend constexpr void swap(view_iterator &a, view_iterator &b) noexcept { a.swap(b); }

		private:
			iter_t m_iter = {};
		};

	public:
		typedef typename view_iterator::value_type value_type;
		typedef typename view_iterator::pointer pointer;
		typedef typename view_iterator::pointer const_pointer;
		typedef typename view_iterator::reference reference;
		typedef typename view_iterator::reference const_reference;
		typedef view_iterator iterator;
		typedef view_iterator const_iterator;
		typedef std::reverse_iterator<view_iterator> reverse_iterator;
		typedef std::reverse_iterator<view_iterator> const_reverse_iterator;
		typedef typename view_iterator::size_type size_type;
		typedef typename view_iterator::difference_type difference_type;

	public:
		/** Initializes an empty view. */
		constexpr component_view() noexcept = default;

		constexpr component_view(const component_view &) = default;
		constexpr component_view &operator=(const component_view &) = default;
		constexpr component_view(component_view &&) noexcept = default;
		constexpr component_view &operator=(component_view &&) noexcept = default;

		/** Initializes component view from pointers to component sets.
		 * @param inc Pointer to component set of included component type.
		 * @param opt Pointers to component sets of optional components. */
		constexpr explicit component_view(set_ptr_t<I> inc, set_ptr_t<O>... opt) : m_set(inc), m_optional(opt...)
		{
			SEK_ASSERT(((inc != nullptr) && ...), "Included component sets can not be null");
		}

		/** Returns iterator to the first entity. */
		[[nodiscard]] constexpr iterator begin() const noexcept
		{
			return m_set != nullptr ? iterator{m_set->begin()} : iterator{};
		}
		/** @copydoc begin */
		[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
		/** Returns iterator one past the last entity. */
		[[nodiscard]] constexpr iterator end() const noexcept
		{
			return m_set != nullptr ? iterator{m_set->end()} : iterator{};
		}
		/** @copydoc end */
		[[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
		/** Returns reverse iterator to the last entity. */
		[[nodiscard]] constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator{end()}; }
		/** @copydoc rbegin */
		[[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
		/** Returns reverse iterator one past the first entity. */
		[[nodiscard]] constexpr reverse_iterator rend() const noexcept { return reverse_iterator{begin()}; }
		/** @copydoc rend */
		[[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

		/** Returns reference to the first entity. */
		[[nodiscard]] constexpr reference front() const noexcept { return *begin(); }
		/** Returns reference to the last entity. */
		[[nodiscard]] constexpr reference back() const noexcept { return *std::prev(end()); }
		/** Returns reference to the entity at offset `i`. */
		[[nodiscard]] constexpr reference at(size_type i) const noexcept
		{
			return begin()[static_cast<difference_type>(i)];
		}
		/** @copydoc at */
		[[nodiscard]] constexpr reference operator[](size_type i) const noexcept { return at(i); }
		/** Checks if the the view is empty. */
		[[nodiscard]] constexpr bool empty() const noexcept { return begin() == end(); }

		/** Returns the size of the of the view. */
		[[nodiscard]] constexpr size_type size() const noexcept { return m_set != nullptr ? m_set->size() : 0; }
		/** @copydoc size */
		[[nodiscard]] constexpr size_type size_hint() const noexcept { return size(); }

		/** Checks if the the view contains the specified entity. */
		[[nodiscard]] constexpr bool contains(entity_t entity) const noexcept
		{
			return m_set != nullptr && m_set->contains(entity);
		}
		/** Returns iterator to the specified entity, or an end iterator if the entity does not belong to the view. */
		[[nodiscard]] constexpr iterator find(entity_t entity) const noexcept
		{
			if (m_set != nullptr) [[likely]]
				return iterator{m_set->find(entity)};
			else
				return end();
		}

		/** Returns pointers to components associated with the entity pointed to by the iterator.
		 *
		 * @param which Iterator to the entity to get components for.
		 * @return Tuple of pointers to entity components, or a single pointer if only one component is specified.
		 * @note Pointers to optional components may be null. */
		template<typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get(const_iterator which) const noexcept
		{
			return get_impl<Ts...>(which);
		}
		/** Returns pointers to components associated with the specified entity.
		 *
		 * @param entity Entity to get components for.
		 * @return Tuple of pointers to entity components, or a single pointer if only one component is specified.
		 *
		 * @note Pointers to optional components may be null.
		 * @warning Using an entity not belonging to the view results in undefined behavior. */
		template<typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get(entity_t entity) const noexcept
		{
			return get_impl<Ts...>(entity);
		}

		/** Applies the functor to every entity of the view.
		 * Functor may optionally return a value, which if evaluated to `false`, prematurely terminates iteration. */
		template<std::invocable<entity_t, I *, O *...> F>
		constexpr void for_each(F &&f) const
		{
			for (auto first = begin(), last = end(); first != last; ++first)
			{
				const auto cmp = std::tuple<I *, O *...>{get<I, O...>(first)};
				const auto e = *first;

				if constexpr (std::convertible_to<std::invoke_result_t<F, entity_t, I *, O *...>, bool>)
				{
					if (!std::invoke(std::forward<F>(f), e, std::get<I *>(cmp), std::get<O *>(cmp)...)) [[unlikely]]
						break;
				}
				else
					std::invoke(std::forward<F>(f), e, std::get<I *>(cmp), std::get<O *>(cmp)...);
			}
		}

		constexpr void swap(component_view &other) noexcept
		{
			using std::swap;
			swap(m_set, other.m_set);
			swap(m_optional, other.m_optional);
		}
		friend constexpr void swap(component_view &a, component_view &b) noexcept { a.swap(b); }

	private:
		template<typename T>
		[[nodiscard]] constexpr auto *get_optional() const noexcept
		{
			return std::get<set_ptr_t<T>>(m_optional);
		}

		template<typename T, typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get_impl(const_iterator which) const noexcept
		{
			if constexpr (sizeof...(Ts) != 0)
				return std::tuple<T *, Ts *...>{get_impl<T>(which), get_impl<Ts>(which)...};
			else if constexpr (is_inc<T>)
				return static_cast<T *>(std::addressof(which.m_iter->second));
			else
				return get_impl<T>(*which);
		}
		template<typename T, typename... Ts>
		[[nodiscard]] constexpr decltype(auto) get_impl(entity_t e) const noexcept
		{
			if constexpr (sizeof...(Ts) != 0)
				return std::tuple<T *, Ts *...>{get_impl<T>(e), get_impl<Ts>(e)...};
			else if constexpr (is_inc<T>)
				return static_cast<T *>(std::addressof(m_set->get(e)));
			else
			{
				const auto set = get_optional<T>();
				if (set == nullptr) [[unlikely]]
					return static_cast<T *>(nullptr);
				const auto pos = set->find(e);
				if (pos == set->end()) [[unlikely]]
					return static_cast<T *>(nullptr);
				return static_cast<T *>(std::addressof(pos->second));
			}
		}

		set_ptr_t<I> m_set;
		opt_ptr m_optional;
	};
}	 // namespace sek::engine