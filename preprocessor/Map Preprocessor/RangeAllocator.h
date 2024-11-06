//
//  RangeAllocator.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 06/11/2024.
//

#pragma once

#include <limits>
#include <map>
#include <unordered_map>

template <typename IntT>
struct Allocation {
	size_t reg;
	IntT value;
};

using Time = int;

template <typename IntT, size_t ReuseThreshold = 2>
class RangeAllocator {
	public:
		/// Takes the total number of available registers.
		RangeAllocator(size_t num_registers) : num_registers_(num_registers) {}

		void add_value(Time time, IntT value) {
			values_.emplace(time, value);
		}

		struct TimeSpan {
			Time begin, end;
		};
		struct ValueSpan {
			TimeSpan time;
			IntT value;
		};

		std::vector<std::vector<ValueSpan>> spans() {
			// Mark all values as not-yet allocated.
			for(auto &value: values_) {
				value.second.is_allocated = false;
			}

			// Greedy algorithm:
			//
			//	(1) find register with largest remaining empty span;
			//	(2) find largest possible fit for that time (if any);
			//	(3) insert that fit, or else mark the time as unusable;
			//	(4) continue until all time and registers are exhausted.
			//
			// This is geared towards output via LD (HL), which means that
			// not all required values actually have to go into registers.
			//
			// Anecdotally: not an amazing algorithm, but acceptable for the
			// limited range of likely integers in a given group and the small
			// size of each group.
		}

	private:
		size_t num_registers_;

		struct Value {
			IntT value{};
			bool is_allocated = false;
		};
		std::map<Time, Value> values_;

		std::optional<ValueSpan> remove_largest_in(TimeSpan span) {
			// Count all non-allocated values within the provided range.
			struct ValueCount {
				TimeSpan span = {.begin = std::numeric_limits<Time>::max(), .end = std::numeric_limits<Time>::min()};
				size_t count = 0;
			};
			using CountMap = std::unordered_map<IntT, ValueCount>;
			CountMap counts;

			auto cursor = values_.lower_bound(span.begin);
			const auto end = values_.upper_bound(span.end);
			while(cursor != end) {
				if(!cursor->second.is_allocated) {
					auto &count = counts[cursor->second.value];
					++count.count;
					count.span.begin = std::min(count.span.begin, cursor->first);
					count.span.end = std::max(count.span.begin, cursor->first);
				}
				++cursor;
			}

			// If that was nothing then the answer is nothing.
			if(counts.empty()) {
				return {};
			}

			// Find the one with the greatest frequency.
			typename CountMap::iterator greatest = counts.begin();
			typename CountMap::iterator current = greatest;
			++current;
			while(current != counts.end()) {
				if(current->second.count > greatest->second.count) {
					greatest = current;
				}
				++current;
			}

			// If even the greatest thing doesn't at least meet the reuse
			// threshold, pass.
			if(greatest->second.count < ReuseThreshold) {
				return {};
			}

			// Otherwise mark values as taken and return the result.

			ValueSpan value_span = {
				.time = greatest->second.span,
				.value = greatest->first
			};
			cursor = values_.lower_bound(span.begin);
			while(cursor != end) {
				cursor->second.is_allocated |= cursor->second.value == value_span.value;
				++cursor;
			}

			return value_span;
		}
};
