//
//  OptionalRegisterAllocator.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 06/11/2024.
//

#pragma once

#include <limits>
#include <map>
#include <unordered_map>

#include "Allocation.h"
#include "Prioritiser.h"

/*!
	Attempts 'reasonably' to allocate registers to a timestamped stream of constants
	with the option of not allocating — i.e. constants don't _have_ to go into registers,
	they can remain just as in-stream constants.

	Furthermore, the template parameter ReuseThreshold provides a minimum number of
	uses that must follow for it to be worth using a register at all.

	Side chat: this therefore models the Z80 scenario of writing out a stream of constants
	via HL, i.e. LD (HL), r is faster but LD (HL), n exists.
*/
template <typename IntT, size_t ReuseThreshold = 2>
class OptionalRegisterAllocator {
	public:
		/// Takes the total number of available registers.
		OptionalRegisterAllocator(size_t num_registers) : num_registers_(num_registers) {}

		void add_value(Time time, IntT value) {
			prioritiser_.add_value(time, value);
		}

		std::vector<Allocation<IntT>> spans() {
			std::vector<Allocation<IntT>> result;
			if(values_.empty()) {
				return result;
			}

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
			struct RegisterState {
				struct AnnotatedValueSpan: public ValueSpan {
					bool is_vacant = false;
				};

				std::map<Time, AnnotatedValueSpan> spans;
				
				TimeSpan largest_unoccupied(Time endpoint) {
					Time location = 0;
					TimeSpan largest = {.begin = 0, .end = 0};
					for(const auto &span: spans) {
						if(span.first - location > largest.length()) {
							largest.begin = location;
							largest.end = span.first;
						}
						location = span.second.time.end;
					}
					
					if(endpoint - location > largest.length()) {
						largest.begin = location;
						largest.end = endpoint;
					}
					
					return largest;
				}
			};
			std::vector<RegisterState> states(num_registers_);
			auto last = values_.end();
			--last;
			const auto end_time = last->first + 1;

			while(true) {
				size_t index = 0;
				TimeSpan range;
				for(size_t c = 0; c < num_registers_; c++) {
					const auto largest = states[c].largest_unoccupied(end_time);
					if(largest.length() > range.length()) {
						range = largest;
						index = c;
					}
				}

				if(!range.length()) break;
				const auto suggestion = remove_largest_in(range);
				if(suggestion) {
					typename RegisterState::AnnotatedValueSpan allocated;
					allocated.time = suggestion->time;
					allocated.value = suggestion->value;
					states[index].spans[range.begin] = allocated;
				} else {
					typename RegisterState::AnnotatedValueSpan vacant;
					vacant.is_vacant = true;
					vacant.time = range;
					states[index].spans[range.begin] = vacant;
				}
			}
			
			// Map down to return type.
			std::map<Time, Allocation<IntT>> allocations;
			size_t reg = 0;
			for(auto &state: states) {
				for(const auto &span: state.spans) {
					if(!span.second.is_vacant) {
						allocations[span.second.time.begin] = Allocation{
							.time = span.second.time.begin,
							.value = span.second.value,
							.reg = reg,
						};
					}
				}
				++reg;
			}

			for(const auto &allocation: allocations) {
				result.push_back(allocation.second);
			}
			return result;
		}

	private:
		size_t num_registers_;
		Prioritiser<IntT> prioritiser_;

		struct Value {
			IntT value{};
			bool is_allocated = false;
		};
		struct ValueSpan {
			TimeSpan time;
			IntT value;
		};
		std::map<Time, Value> values_;

		std::optional<ValueSpan> remove_largest_in(TimeSpan span) {
			// TODO: use the prioritiser to find whatever has the longest run
			// of being highest priority.

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
