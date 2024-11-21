//
//  MandatoryRegisterAllocator.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 09/11/2024.
//

#pragma once

#include "Allocation.h"
#include "Prioritiser.h"
#include "Register.h"
#include "RegisterSet.h"

#include <map>
#include <optional>
#include <vector>

/*!
	Attempts 'reasonably' to allocate registers to a timestamped stream of constants
	with the requirement that all constants must go into a register when they arrive.

	Side chat: this therefore models the Z80 scenario of writing out a stream of constants
	via SP, as the only way to write a value is by first putting it into a register.

	TODO: treat index registers as second class.
*/
template <typename IntT>
class MandatoryRegisterAllocator {
public:
	template<typename ListT>
	MandatoryRegisterAllocator(const ListT &registers) :
		registers_(registers) {}

	void add_value(Time time, IntT value) {
		prioritiser_.add_value(time, value);
	}

	std::vector<Allocation<IntT>> spans() {
		// Dumb algorithm: at each time that a value is needed,
		// evict whichever held value has the lowest priority.
		std::vector<Allocation<IntT>> spans;
		std::map<Register::Name, Allocation<IntT>*> active_allocations_;

		for(const auto &pair: prioritiser_.values()) {
			const auto allocate = [&](Register::Name reg) {
				auto &allocation = spans.emplace_back();
				allocation.value = pair.second;
				allocation.time = pair.first;
				allocation.reg = reg;
				active_allocations_[reg] = &allocation;
				state_.set_value(reg, pair.second);
			};

			// Is this a reuse or possibly a new allocation?
			bool resolved = false;
			for(auto reg: registers_) {
				const auto value = state_.value<IntT>(reg);
				if(value) {
					if(*value == pair.second) {
						resolved = true;
						break;
					}
				} else {
					allocate(reg);
					resolved = true;
					break;
				}
			}

			if(resolved) continue;

			// All registers are allocated, none already has the
			// desired value. So something will need to be evicted.
			// Find whatever has the lowest priority when limited to
			// the time range for which the new value persists.
			const auto interesting_span = *prioritiser_.span_of(pair.second, pair.first);

			Register::Name selected = Register::Name::SP;			// A clearly invalid value.
			int min_priority = std::numeric_limits<int>::max();
			for(auto reg: registers_) {
				const auto current_priority =
					prioritiser_.priority_at(
						pair.first,
						interesting_span.end,
						*state_.value<IntT>(reg)
					);

				// In this case the value that's in that register
				// is due to be evicted anyway. That was lucky!
				if(!current_priority) {
					selected = reg;
					break;
				}

				if(*current_priority < min_priority) {
					min_priority = *current_priority;
					selected = reg;
				}
			}
			allocate(selected);
		}

		return spans;
	}

private:
	RegisterSet state_;
	std::vector<Register::Name> registers_;
	Prioritiser<IntT> prioritiser_;
};
