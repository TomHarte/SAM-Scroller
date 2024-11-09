//
//  Allocation.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 09/11/2024.
//

#pragma once

using Time = int;

struct TimeSpan {
	Time begin = 0, end = 0;
	Time length() const {	return end - begin;	}
};

template <typename IntT>
struct Allocation {
	Time time;
	IntT value;
	size_t reg;
};
