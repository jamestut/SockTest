#pragma once

#include <stdint.h>
#include <algorithm>

constexpr uint64_t SUGGEST_INITIAL = 0xCAFEBABEDEADBEEFULL;

uint64_t xorshift64(uint64_t state)
{
	uint64_t x = state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}

void fillRandom(void* buff, size_t sz) {
	uint64_t v = SUGGEST_INITIAL;
	size_t wordCount = sz / sizeof(uint64_t);
	uint64_t* wBuff = reinterpret_cast<uint64_t*>(buff);
	std::generate_n(wBuff, wordCount, [&v]() {
		return (v = xorshift64(v));
	});
	
	// fill the remaining data
	if (sz % sizeof(uint64_t)) {
		v = xorshift64(v);
		void* remBuff = reinterpret_cast<void*>(wBuff + wordCount);
		memcpy(remBuff, &v, sz % sizeof(uint64_t));
	}
}