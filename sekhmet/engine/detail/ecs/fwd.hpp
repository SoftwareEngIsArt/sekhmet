/*
 * Created by switchblade on 19/07/22
 */

#pragma once

#include <memory>

namespace sek::engine
{
	class entity_t;

	template<typename>
	struct component_traits;

	template<typename, typename>
	class basic_entity_set;
	template<typename>
	class component_set;

	template<typename...>
	struct order_by_t;
	template<typename...>
	struct included_t;
	template<typename...>
	struct optional_t;
	template<typename...>
	struct excluded_t;

	template<typename...>
	class entity_query;

	template<typename, typename, typename>
	class component_view;
	template<typename...>
	class basic_component_collection;

	class entity_world;
}	 // namespace sek::engine