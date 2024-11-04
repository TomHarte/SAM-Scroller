//
//  SpriteSerialiser.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 04/11/2024.
//

#pragma once

#include "PixelAccessor.h"

class SpriteSerialiser {
	public:
		SpriteSerialiser(PixelAccessor &accessor) : accessor_(accessor) {
			// Evaluate cost of using HL as the output cursor.
			int hl_cost = 0;
			int hl = 0;
			for(size_t y = 0; y < accessor.height(); y ++) {
				for(size_t x = 0; x < accessor.width(); x += 2) {
					const uint32_t left = accessor.pixel(x, y);
					const uint32_t right = accessor.pixel(x + 1, y);

					// Skip transparent pixels.
					if((left >> 24) == 0 && (right >> 24) == 0) {
						continue;
					}

					int required_hl = static_cast<int>(((y << 8) + x) >> 1);
					if(required_hl == hl+1) {
						hl_cost += 4;	// INC L
					} else {
						hl_cost += 17;	// LD BC, nn; ADD HL, BC
					}
					hl_cost += 10;		// LD (HL), n
					hl = required_hl;
				}
			}

			// Evaluate cost of using IX as the output cursor.
			int ix_cost = 16;	// Loading cost.
			for(size_t y = 0; y < accessor.height(); y ++) {
				if(y&1) ix_cost += 8;	// INC IXh

				for(size_t x = 0; x < accessor.width(); x += 2) {
					const uint32_t left = accessor.pixel(x, y);
					const uint32_t right = accessor.pixel(x + 1, y);

					// Skip transparent pixels.
					if((left >> 24) == 0 && (right >> 24) == 0) {
						continue;
					}
					ix_cost += 19;		// LD (IX+d), n
				}
			}

			// Evaluate cost of using the stack pointer.
//			int sp_cost = 4 + 10 + 6;	// Loading cost.
//			int sp = 0;
//			for(size_t y = accessor.height() - 1; y >= 0; y --) {
//				for(size_t x = accessor.width(); x >= 0; x -= 2) {
//					const uint32_t left = accessor.pixel(x, y);
//					const uint32_t right = accessor.pixel(x + 1, y);
//				}
//			}

			strategy = hl_cost < ix_cost ? OutputStrategy::HL : OutputStrategy::IX;
		}

	private:
		PixelAccessor &accessor_;

		enum class OutputStrategy {
			IX, HL,
		} strategy;

//		std::optional<uint8_t> pixels_at(size_t x, size_t y) {
//			const uint32_t left = accessor.pixel(x, y);
//			const uint32_t right = accessor.pixel(x + 1, y);
//
//			if((left >> 24) == 0 && (right >> 24) == 0) return {};
//			return
//		}
};

