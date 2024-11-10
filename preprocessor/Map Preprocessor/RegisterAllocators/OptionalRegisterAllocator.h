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
			Prioritiser<IntT> prioritiser = prioritiser_;
			std::vector<Allocation<IntT>> result;

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
			const auto end_time = prioritiser.end_time();

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
				const auto suggestion = remove_largest_in(range, prioritiser);
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

		struct ValueSpan {
			TimeSpan time;
			IntT value;
		};

		static std::optional<ValueSpan> remove_largest_in(TimeSpan span, Prioritiser<IntT> &prioritiser) {
			// Use the prioritiser to find whatever has the longest run
			// of being highest priority and the range over which it
			// has the highest priority. Then eliminate that value within
			// this range and continue.
			struct PriorityCount {
				TimeSpan range = {
					.begin = std::numeric_limits<Time>::max(),
					.end = std::numeric_limits<Time>::min(),
				};
				size_t count;
			};
			std::unordered_map<IntT, PriorityCount> priority_wins;
			IntT top_value = 0;
			size_t top_priority = 0;
			for(Time time = span.begin; time < span.end; time++) {
				const auto at_time = prioritiser.prioritised_value_at(time);
				if(!at_time) continue;
				if(at_time->usages_remaining < ReuseThreshold) continue;
				
				auto &priority = priority_wins[at_time->value];
				priority.range.begin = std::min(priority.range.begin, time);
				priority.range.end = std::max(priority.range.end, time);
				++priority.count;
				if(priority.count > top_priority) {
					top_priority = priority.count;
					top_value = at_time->value;
				}
			}

			if(priority_wins.empty()) return {};
			auto &priority_winner = priority_wins[top_value];
			prioritiser.remove_value(priority_winner.range.begin, priority_winner.range.end, top_value);

			return ValueSpan {
				.time = priority_winner.range,
				.value = top_value,
			};
		}
};
