#pragma once

#include <cstdlib>
#include <random>

class RandomState : public std::mt19937 {
public:
	std::random_device rd;
	RandomState() :rd(), std::mt19937(rd()) {}
};

class Random {
	inline static RandomState state{};
	int maxVal;
public:
	Random(int maxVal) : maxVal(maxVal) {}

	inline operator int() {
		//decltype(uniform_dist.param()) new_range (0, upper);
		//uniform_dist.param(new_range);

		std::uniform_int_distribution<int> distrib(0, maxVal);
		return distrib(state);
	}
};
