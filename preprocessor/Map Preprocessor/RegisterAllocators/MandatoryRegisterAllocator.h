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
	MandatoryRegisterAllocator(const ListT &registers) {
		for(const auto reg: registers) {
			if(Register::is_index_pair(reg)) {
				index_registers_.push_back(reg);
			} else {
				registers_.push_back(reg);
			}
		}
	}

	void add_value(Time time, IntT value) {
		prioritiser_.add_value(time, value);
	}

	// TODO: switch signature of spans, and downstream where necessary, so that this class can posit potential
	// span lists and get feedback on which is more efficient for gradient optimisation. At the minute it just
	// makes a guess of its own.

	std::vector<Allocation<IntT>> spans() {
		// Do an initial run to set a no-index-register baseline.
		std::vector<Allocation<IntT>> baseline = spans({});
		if(index_registers_.empty()) {
			return baseline;
		}

		// Henceforth maintain a set of spilled numbers which don't improve
		// life if moved into index registers and those which do, and try using
		// an optional allocator on those which do.
		//
		// Continue until either all spilled numbers have vanished, or returns
		// have stopped being found.
		std::unordered_set<IntT> extracted;
		std::unordered_set<IntT> dont_extract;

		while(true) {
			// Get list of new spills, ordered by frequency.
			std::unordered_map<IntT, size_t> spills;
			for(const auto &allocation: baseline) {
				--spills[allocation.value];	// Use negative values because I cannot be bothered writing a custom
											// orderer for the multimap below.
			}

			std::multimap<size_t, IntT> ordered_spills;
			for(const auto &spill: spills) {
				if(spill.first < 2) {
					// Simple metric: if this 'spill' is actually loaded only a grand total of once,
					// it isn't going to be improved by being loaded to an index register as nothing
					// else seems to compete with it.
					continue;
				}
				ordered_spills.emplace(spill.second, spill.first);
			}

			if(ordered_spills.empty()) {
				return baseline;
			}

			// If spills count is less than some threshold, brute force it.
			//
			// Otherwise take the top item, check whether it helps, and put it
			// either into the extracted or the dont_extract set. Then continue.
			if(ordered_spills.size() < 10) {
				printf("Brute forcing: %zu\n", ordered_spills.size());
				for(int try_list = 1; try_list < 1 << ordered_spills.size(); try_list++) {
					OptionalRegisterAllocator<uint16_t> index_allocator(index_registers_);

					int index = 1;
					for(const auto &spill: ordered_spills) {
						if(try_list & index) {
							for(const auto &pair: prioritiser_.values()) {
								if(pair.second == spill.second) {
									index_allocator.add_value(pair.first, pair.second);
								}
							}
						}
						index <<= 1;
					}

					const auto index_spans = index_allocator.spans();
					if(index_spans.empty()) continue;	// Non-use of the index registers has already been tested.

					const auto new_encoding = spans(index_spans);
					if(cost(new_encoding) < cost(baseline)) {
						baseline = new_encoding;
					}
				}

				return baseline;
			} else {
				printf("Should search: %zu\n", ordered_spills.size());
				break;
			}
		}

		// TODO: some sort of zip.

		return baseline;
	}

private:
	std::vector<Register::Name> registers_;
	std::vector<Register::Name> index_registers_;
	Prioritiser<IntT> prioritiser_;

	size_t cost(const std::vector<Allocation<IntT>> &spans) {
		size_t result = 0;
		auto it = spans.begin();
		RegisterSet state;
		for(const auto &value: prioritiser_.values()) {
			// Perform a load if required.
			if(it != spans.end() && it->time == value.first) {
				state.set_value<uint16_t>(it->reg, it->value);
				result += Register::is_index_pair(it->reg) ? 4 : 3;
				++it;
			}

			// Test which register is being drawn from.
			const auto ix = state.value<uint16_t>(Register::Name::IX);
			const auto iy = state.value<uint16_t>(Register::Name::IY);
			const bool is_index = (ix && *ix == value.second) || (iy && *iy == value.second);
			result += is_index ? 4 : 3;
		}

		return result;
	}

	std::vector<Allocation<IntT>> spans(const std::vector<Allocation<IntT>> &index_reservations) {
		// Dumb algorithm: at each time that a value is needed,
		// evict whichever held value has the lowest priority.
		//
		// Use the provided set of index_reservations to maintain
		// a set of values that are in the index registers.
		RegisterSet state;
		std::vector<Allocation<IntT>> spans;
		std::map<Register::Name, Allocation<IntT>*> active_allocations_;
		auto index_cursor = index_reservations.begin();

		std::vector<Register::Name> all_registers = registers_;
		all_registers.insert(all_registers.end(), index_registers_.begin(), index_registers_.end());

		for(const auto &pair: prioritiser_.values()) {
			const auto allocate = [&](Register::Name reg) {
				auto &allocation = spans.emplace_back();
				allocation.value = pair.second;
				allocation.time = pair.first;
				allocation.reg = reg;
				active_allocations_[reg] = &allocation;
				state.set_value(reg, pair.second);
			};

			// Apply any hit index reservations, copying them into the result.
			if(index_cursor != index_reservations.end() && index_cursor->time == pair.first) {
				spans.push_back(*index_cursor);
				state.set_value<uint16_t>(index_cursor->reg, index_cursor->value);
				++index_cursor;
			}

			// Is this a reuse or possibly a new allocation?
			bool resolved = false;
			for(auto reg: all_registers) {
				const auto value = state.value<IntT>(reg);
				if(value && *value == pair.second) {
					resolved = true;
					break;
				}
			}
			if(resolved) continue;

			for(auto reg: registers_) {
				const auto value = state.value<IntT>(reg);
				if(!value) {
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
						*state.value<IntT>(reg)
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
};
