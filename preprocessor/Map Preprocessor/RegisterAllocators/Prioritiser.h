//
//  Prioritiser.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 09/11/2024.
//

#pragma once

#include <optional>
#include <map>
#include <unordered_set>

/*!
	Based on a list of values and the times they're required at,
	provides the single value that would most benefit from being
	in a register at any given point in time along with the number of
	times it will be used at any point in the future.
*/
template <typename IntT>
class Prioritiser {
public:
	void add_value(Time time, IntT value) {
		values_.emplace(time, value);
	}

	void remove_value(Time start, Time end, IntT value) {
		auto cursor = values_.lower_bound(start);
		const auto target = values_.upper_bound(end);
		while(cursor != target) {
			if(cursor->second == value) {
				cursor = values_.erase(cursor);
			} else {
				++cursor;
			}
		}
	}

	struct PrioritisedValue {
		IntT value;
		size_t usages_remaining;	// Provided for the benefit of determining whether
									// this value ever deserves to be in a register,
									// supposing that's relevant.
	};
	std::optional<PrioritisedValue> prioritised_value_at(Time time) {
		// Version 1 has a very simple metric: number of remaining usages.

		// Count remaining usages
		std::map<IntT, size_t> counts;
		auto cursor = values_.lower_bound(time);
		while(cursor != values_.end()) {
			++counts[cursor->second];
			++cursor;
		}

		// Pick whatever came out on top, if anything.
		if(counts.empty()) {
			return {};
		}

		auto value = counts.end();
		--value;
		return PrioritisedValue {
			.value = value->first,
			.usages_remaining = value->second
		};
	}

private:
	std::map<Time, IntT> values_;
};

