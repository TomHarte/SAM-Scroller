//
//  Prioritiser.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 09/11/2024.
//

#pragma once

#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

template <typename IntT>
struct PrioritisedValue {
	IntT value;
	TimeSpan active_range = {
		.begin = std::numeric_limits<Time>::max(),
		.end = std::numeric_limits<Time>::min(),
	};
	size_t usages_remaining = 0;	// Provided for the benefit of determining whether
									// this value ever deserves to be in a register,
									// supposing that's relevant.
};

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

	Time end_time() const {
		if(values_.empty()) return 0;
		auto back = values_.end();
		--back;
		return back->first + 1;
	}

	std::optional<TimeSpan> span_of(IntT value, Time start = 0) const {
		// Do a dumb, linear search.
		TimeSpan result{
			.begin = std::numeric_limits<Time>::max(),
			.end = std::numeric_limits<Time>::min(),
		};

		auto it = values_.lower_bound(start);
		while(it != values_.end()) {
			if(it->second == value) {
				result.begin = std::min(result.begin, it->first);
				result.end = std::max(result.begin, it->first);
			}
			++it;
		}

		if(result.begin > result.end) {
			return {};
		}
	}

	std::optional<int> priority_at(Time time, Time horizon, IntT value) const {
		IntT throwaway;
		const auto priorities = all_priorities(time, horizon, throwaway);
		const auto entry = priorities.find(value);
		if(entry == priorities.end()) {
			return {};
		}
		return entry->second.usages_remaining;
	}

	std::optional<PrioritisedValue<IntT>> prioritised_value_at(Time time, Time horizon) const {
		// Version 1 has a very simple metric: number of remaining usages.
		// TODO: incorporate a sense of how imminent those usages are.

		// Count remaining usages, keeping track of the top item.
		IntT top_value = 0;
		const auto priorities = all_priorities(time, horizon, top_value);

		// Pick whatever came out on top, if anything.
		if(priorities.empty()) {
			return {};
		}
		return priorities.find(top_value)->second;
	}

	const std::map<Time, IntT> &values() {
		return values_;
	}

private:
	std::map<Time, IntT> values_;

	std::unordered_map<IntT, PrioritisedValue<IntT>> all_priorities(Time time, Time horizon, IntT &top_value) const {
		std::unordered_map<IntT, PrioritisedValue<IntT>> priorities;

		size_t top_count = 0;
		auto cursor = values_.lower_bound(time);
		const auto target = values_.upper_bound(horizon);
		while(cursor != target) {
			auto &priority = priorities[cursor->second];

			priority.value = cursor->second;
			priority.active_range.begin = std::min(priority.active_range.begin, cursor->first);
			priority.active_range.end = std::max(priority.active_range.end, cursor->first + 1);

			++priority.usages_remaining;
			if(priority.usages_remaining > top_count) {
				top_count = priority.usages_remaining;
				top_value = cursor->second;
			}
			++cursor;
		}

		return priorities;
	}

};

