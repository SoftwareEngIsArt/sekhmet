//
// Created by switchblade on 2021-12-16.
//

#include <gtest/gtest.h>

#include "sekhmet/math.hpp"

static_assert(std::is_trivially_copyable_v<sek::math::vector4d>);

TEST(math_tests, vec_test)
{
	{
		sek::math::vector4d v4_1 = {0, 0, 0, 0}, v4_2 = {1, 2, 3, 4};
		auto v4_3 = v4_1 + v4_2;
		EXPECT_EQ(v4_3, v4_2);
		EXPECT_EQ(sek::math::dot(v4_3, v4_2), 1 + 2 * 2 + 3 * 3 + 4 * 4);
		EXPECT_EQ(abs(sek::math::vector4d{-1.0, 2.0, 3.0, 4.0}), (sek::math::vector4d{1.0, 2.0, 3.0, 4.0}));
		EXPECT_EQ(max(v4_3, v4_1), v4_2);
	}

	{
		using v16d = sek::math::basic_vector<double, 16>;
		auto v16_1 = v16d{1.}, v16_2 = v16d{2.}, v16_3 = v16d{3.};
		auto v16_4 = v16_1 + v16_2;
		EXPECT_EQ(v16_4, v16_3);
	}

	{
		sek::math::vector2i v2i_1 = {1, 0}, v2i_2 = {0, -1};
		EXPECT_EQ(v2i_1 + v2i_2, sek::math::vector2i(1, -1));
		EXPECT_EQ(abs(v2i_1 + v2i_2), sek::math::vector2i(1, 1));
	}

	{
		using v6f = sek::math::basic_vector<float, 6>;
		v6f v6f_1 = v6f{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
		auto v6f_2 = sek::math::dot(v6f_1, v6f_1);
		EXPECT_EQ(v6f_2, 1.f + 2.f * 2.f + 3.f * 3.f + 4.f * 4.f + 5.f * 5.f + 6.f * 6.f);
	}

	{
		sek::math::vector3d v3d_1 = {1, 2, 3};
		EXPECT_EQ(sek::math::dot(v3d_1, v3d_1), 1 + 2 * 2 + 3 * 3);
		auto v3d_2 = sek::math::cross(v3d_1, {4, 5, 6});
		EXPECT_EQ(v3d_2, (sek::math::vector3d{-3, 6, -3}));
	}

	{
		sek::math::vector3f v3f_1 = {1, 2, 3};
		auto n1 = sek::math::norm(v3f_1);
		auto n2 = v3f_1 / sek::math::magn(v3f_1);
		EXPECT_EQ(n1, n2);

		auto v3f_2 = sek::math::cross(v3f_1, {4, 5, 6});
		EXPECT_EQ(v3f_2, (sek::math::vector3f{-3, 6, -3}));
	}

	{
		sek::math::vector3f v3f = {1, 2, 3};

		EXPECT_EQ((sek::math::shuffle<2, 1>(v3f)), (sek::math::vector2f{3, 2}));
		EXPECT_EQ((sek::math::shuffle<0, 1, 2, 2>(v3f)), (sek::math::vector4f{1, 2, 3, 3}));
	}

	{
		sek::math::vector2d v2d = {1, 2};
		EXPECT_EQ((sek::math::shuffle<1, 0>(v2d)), (sek::math::vector2d{2, 1}));
		EXPECT_EQ((sek::math::shuffle<1, 0, 0>(v2d)), (sek::math::vector3d{2, 1, 1}));
	}
}

TEST(math_tests, random_test)
{
	{
		sek::math::xoroshiro256<uint64_t> r1;
		sek::math::xoroshiro256<uint64_t> r2 = r1;

		EXPECT_EQ(r1, r2);
		EXPECT_EQ(r1(), r2());
		EXPECT_NE(r1(), r1());
	}

	{
		sek::math::xoroshiro128<float> r1;
		sek::math::xoroshiro128<float> r2;

		EXPECT_EQ(r1(), r2());
		EXPECT_NE(r1(), r1());

		std::stringstream ss;
		ss << r1;
		ss >> r2;
		EXPECT_EQ(r1(), r2());
	}
}