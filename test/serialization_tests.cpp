//
// Created by switchblade on 2022-04-15.
//

#include <gtest/gtest.h>

#include "sekhmet/serialization/ubjson.hpp"

namespace ser = sek::serialization;

TEST(serialization_tests, base64_test)
{
	struct data_t
	{
		constexpr bool operator==(const data_t &) const noexcept = default;

		int i;
		float f;
	} data = {1234, std::numbers::pi_v<float>}, decoded;

	auto len = ser::base64_encode<char16_t>(&data, sizeof(data), nullptr);
	auto buff = new char16_t[len];
	ser::base64_encode(&data, sizeof(data), buff);

	EXPECT_TRUE(ser::base64_decode(&decoded, sizeof(decoded), buff, len));
	EXPECT_EQ(decoded, data);

	delete[] buff;
}

namespace
{
	struct serializable_t
	{
		void serialize(auto &archive) const
		{
			archive << ser::named_entry("n", nullptr);
			archive << ser::named_entry("s", s);
			archive << ser::named_entry("i", i);
			archive << ser::named_entry("m", m);
			archive << ser::named_entry("b", b);
			archive << v << p;
		}
		void deserialize(auto &archive)
		{
			archive >> ser::named_entry("n", nullptr);
			archive >> ser::named_entry("s", s);
			archive >> ser::named_entry("i", i);
			archive >> ser::named_entry("m", m);
			archive >> ser::named_entry("b", b);
			archive >> v >> p;
		}

		bool operator==(const serializable_t &) const noexcept = default;

		std::string s;
		int i;
		bool b;
		std::vector<int> v;
		std::pair<int, float> p;
		std::map<std::string, int> m;
	};
}	 // namespace

TEST(serialization_tests, ubjson_test)
{
	constexpr auto print_ubc_data = [](const char *bytes, std::size_t n) noexcept -> void
	{
		for (std::size_t i = 0; i < n; ++i)
		{
			auto c = bytes[i];
			if (isprint(c))
				putc(c, stdout);
			else
				printf("\\x%02x", c);
		}
		putc('\n', stdout);
	};

	const serializable_t data = {
		.s = "Hello, world!",
		.i = 0x420,
		.b = true,
		.v = {0, 1, 2, 3},
		.p = {69, 420.0f},
		.m = {{"i1", 1}, {"i2", 2}},
	};

	std::string ubj_string;
	{
		std::stringstream ss;
		ser::ubj::output_archive archive{ss};
		archive << data;

		archive.flush();
		ubj_string = ss.str();
	}
	print_ubc_data(ubj_string.data(), ubj_string.size());
	serializable_t deserialized;
	{
		ser::ubj::input_archive archive{ubj_string.data(), ubj_string.size()};
		archive >> deserialized;
	}

	EXPECT_EQ(data, deserialized);
}