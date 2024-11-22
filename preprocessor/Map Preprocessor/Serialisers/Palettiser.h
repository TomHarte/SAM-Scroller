//
//  Palettiser.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 22/11/2024.
//

#pragma once

#include <algorithm>
#include <set>

struct Palette {
	std::vector<uint8_t> sam_palette;
	std::unordered_map<uint32_t, uint8_t> source_mapping;
};

template <size_t TargetCount = 16, size_t TargetOffset = 0>
class Palettiser {
public:
	Palettiser(size_t rotation) : rotation_(rotation) {}

	void add_colour(uint32_t colour) {
		colours_.insert(colour);
	}

	void add_colours(const PixelAccessor &source) {
		for(size_t y = 0; y < source.height(); y++) {
			const uint32_t *line = source.pixels(0, y);
			colours_.insert(line, line + source.width());
		}
	}

	Palette palette() {
		// First effort: median cut.
		// Seed output palette with initial bucket.
		struct Bucket {
			std::vector<uint32_t> colours;
		};
		std::vector<Bucket> buckets;

		auto &bucket = buckets.emplace_back();
		std::transform(
			colours_.begin(),
			colours_.end(),
			std::back_inserter(bucket.colours),
			[](const auto value){ return value;}
		);

		while(true) {
			// Test for termination conditions: at 16 buckets, or
			// else all remaining buckets have only one colour in them.
			if(buckets.size() == TargetCount) break;
			size_t net_size = 0;
			for(const auto &bucket: buckets) {
				net_size += bucket.colours.size();
			}
			if(net_size == buckets.size()) break;

			// Didn't terminate so find bucket with greatest single-colour range.
			int colour_shift = 0;
			Bucket *selected_bucket = nullptr;
			int max_range = 0;
			for(auto &bucket: buckets) {
				if(bucket.colours.size() == 1) {
					continue;
				}

				uint32_t max = 0, min = 255;
				for(int index = 0; index < 24; index += 8) {
					for(const auto colour: bucket.colours) {
						max = std::max(max, (colour >> index) & 0xff);
						min = std::min(min, (colour >> index) & 0xff);
					}

					const auto range = max - min;
					if(range > max_range) {
						selected_bucket = &bucket;
						colour_shift = index;
						max_range = range;
					}
				}
			}

			// Sort values in that bucket by the relevant channel, divide and continue.
			std::multimap<uint8_t, uint32_t> colours_by_channel;
			for(const auto colour: selected_bucket->colours) {
				colours_by_channel.emplace((colour >> colour_shift) & 0xff, colour);
			}

			auto median = colours_by_channel.begin();
			std::advance(median, colours_by_channel.size() / 2);

			selected_bucket->colours.clear();
			auto cursor = colours_by_channel.begin();
			while(cursor != median) {
				selected_bucket->colours.push_back(cursor->second);
				++cursor;
			}

			auto &new_bucket = buckets.emplace_back();
			while(cursor != colours_by_channel.end()) {
				new_bucket.colours.push_back(cursor->second);
				++cursor;
			}
		}

		// Build final palette.
		Palette result;
		result.sam_palette.resize(TargetCount);
		uint8_t palette_index = rotation_;
		for(const auto &bucket: buckets) {
			const uint8_t index = palette_index + TargetOffset;
			palette_index = (palette_index + 1) % TargetCount;

			uint32_t sum[3]{};
			for(const auto colour: bucket.colours) {
				result.source_mapping[colour] = index;
				sum[0] += (colour >> 0) & 0xff;
				sum[1] += (colour >> 8) & 0xff;
				sum[2] += (colour >> 16) & 0xff;
			}

			sum[0] = uint8_t(roundf(static_cast<float>(sum[0]) / static_cast<float>(bucket.colours.size())));
			sum[1] = uint8_t(roundf(static_cast<float>(sum[1]) / static_cast<float>(bucket.colours.size())));
			sum[2] = uint8_t(roundf(static_cast<float>(sum[2]) / static_cast<float>(bucket.colours.size())));

			const auto remap = [](uint32_t source) {
				return int(roundf(7.0 * static_cast<float>(source) / 255.0));
			};

			const uint8_t red = remap(sum[0]);
			const uint8_t green = remap(sum[1]);
			const uint8_t blue = remap(sum[2]);

			const uint8_t bright = (red & 1) + (green & 1) + (blue & 1);
			const uint8_t sam_colour =
				((green & 4) ? 0x40 : 0x00) |
				((red & 4) ? 0x20 : 0x00) |
				((blue & 4) ? 0x10 : 0x00) |

				((green & 2) ? 0x08 : 0x00) |
				((red & 2) ? 0x04 : 0x00) |
				((blue & 2) ? 0x02 : 0x00) |

				// Rule for bright: at least two voted for it, and none were zero
				// (as to me it looks odder to introduce red, green or blue
				// when there is meant to be none than it does to have the whole
				// colour be slightly darker.
				((bright >= 2 && red && green && blue) ? 0x01 : 0x00);
			result.sam_palette[index] = sam_colour;
		}

		return result;
	}

private:
	std::unordered_set<uint32_t> colours_;
	size_t rotation_;
};
