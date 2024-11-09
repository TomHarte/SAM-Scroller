//
//  Allocation.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 09/11/2024.
//

using Time = int;

template <typename IntT>
struct Allocation {
	Time time;
	IntT value;
	size_t reg;
};
