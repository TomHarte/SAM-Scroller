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
#include <vector>

#include "Allocation.h"
#include "Prioritiser.h"
#include "Register.h"

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
	/// Takes a list of permissible registers, in any form that can construct a vector.
	template<typename ListT>
	OptionalRegisterAllocator(const ListT &registers) :
		registers_(registers) {}

	void add_value(Time time, IntT value) {
		prioritiser_.add_value(time, value);
	}

	std::vector<Allocation<IntT>> spans() {
		auto prioritiser = prioritiser_;
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
			struct AnnotatedValueSpan: public PrioritisedValue<IntT> {
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
					location = span.second.active_range.end;
				}

				if(endpoint - location > largest.length()) {
					largest.begin = location;
					largest.end = endpoint;
				}

				return largest;
			}
		};
		std::vector<RegisterState> states(registers_.size());
		const auto end_time = prioritiser.end_time();

		while(true) {
			size_t index = 0;
			TimeSpan range;
			for(size_t c = 0; c < registers_.size(); c++) {
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
				allocated.active_range = suggestion->active_range;
				allocated.value = suggestion->value;
				states[index].spans[range.begin] = allocated;
			} else {
				typename RegisterState::AnnotatedValueSpan vacant;
				vacant.is_vacant = true;
				vacant.active_range = range;
				states[index].spans[range.begin] = vacant;
			}
		}

		// Map down to return type.
		std::map<Time, Allocation<IntT>> allocations;
		size_t reg = 0;
		for(auto &state: states) {
			for(const auto &span: state.spans) {
				if(!span.second.is_vacant) {
					allocations[span.second.active_range.begin] = Allocation{
						.time = span.second.active_range.begin,
						.value = span.second.value,
						.reg = registers_[reg],
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
	std::vector<Register::Name> registers_;
	Prioritiser<IntT> prioritiser_;

	static std::optional<PrioritisedValue<IntT>> remove_largest_in(TimeSpan span, Prioritiser<IntT> &prioritiser) {
		const auto at_time = prioritiser.prioritised_value_at(span.begin, span.end);
		if(!at_time) return {};
		if(at_time->usages_remaining < ReuseThreshold) return {};
		prioritiser.remove_value(at_time->active_range.begin, at_time->active_range.end, at_time->value);
		return *at_time;
	}

};
