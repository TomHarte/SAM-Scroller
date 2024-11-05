//
//  SpriteSerialiser.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 04/11/2024.
//

#pragma once

#include "PixelAccessor.h"

struct SpriteEvent {
	enum class Type {
		/// Moves the cursor location.
		Move,
		/// Writes a uint8_t value and advances one position to the right.
		OutputByte,
		/// Stop outputting. Subsequent events will be undefined.
		Stop
	};
	Type type = Type::Stop;

	union {
		uint8_t output;
		struct {
			size_t x;
			size_t y;
		} move;
	} content;
};
class SpriteSerialiser {
	public:
		SpriteSerialiser(
			uint8_t index,
			const PixelAccessor &accessor,
			const std::map<uint32_t, uint8_t> &palette) :
				index_(index),
				contents_(accessor, palette, false)
		{
			// TODO: reinstate the below, with some sense of register allocation,
			// owned elsewhere.

/*			// Evaluate cost of using HL as the output cursor.
			int hl_cost = 0;
			int hl = 0;
			for(size_t y = 0; y < accessor.height(); y ++) {
				for(size_t x = 0; x < accessor.width(); x += 2) {
					if(!pixels_at(x, y)) {
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

			// TODO: LD (HL), r is faster than LD (HL), n, so write and test
			// some sort of register allocator here to find out what it can
			// reduce the LD (HL) code to, for fair comparison.
			//
			// Conversely, LD (ix+d), n seems to cost 'the same' as LD (ix+d), r
			// (modulo the Sam's limited memory availability) so possibly not as
			// vital there.

			// Evaluate cost of using IX as the output cursor.
			int ix_cost = 16;	// Loading cost.
			for(size_t y = 0; y < accessor.height(); y ++) {
				if(y&1) ix_cost += 8;	// INC IXh

				for(size_t x = 0; x < accessor.width(); x += 2) {
					if(!pixels_at(x, y)) {
						continue;
					}
					ix_cost += 19;		// LD (IX+d), n
				}
			}

			// TODO: evaluate cost of using the stack pointer.
//			int sp_cost = 4 + 10 + 6;	// Loading cost.
//			int sp = 0;
//			for(size_t y = accessor.height() - 1; y >= 0; y --) {
//				for(size_t x = accessor.width(); x >= 0; x -= 2) {
//					const uint32_t left = accessor.pixel(x, y);
//					const uint32_t right = accessor.pixel(x + 1, y);
//				}
//			}

			strategy = hl_cost < ix_cost ? OutputStrategy::HL : OutputStrategy::IX;*/
		}

		SpriteEvent next() {
			if(enqueued_) {
				const auto result = *enqueued_;
				enqueued_ = {};
				return result;
			}

			while(y_ < contents_.height()) {
				while(x_ < contents_.width()) {
					const auto x = x_;
					x_ += 2;

					const auto pixels = pixels_at(x, y_);
					if(pixels) {
						SpriteEvent output = {
							.type = SpriteEvent::Type::OutputByte,
							.content.output = *pixels
						};

						if(continuous_) {
							return output;
						}

						continuous_ = true;
						enqueued_ = output;
						return SpriteEvent{
							.type = SpriteEvent::Type::Move,
							.content.move.x = x >> 1,
							.content.move.y = y_,
						};
					}

					continuous_ = false;
				}

				++y_;
				x_ = 0;
				continuous_ = false;
			}

			return SpriteEvent{.type = SpriteEvent::Type::Stop};
		}

		void reset() {
			x_ = y_ = 0;
			continuous_ = true;
			enqueued_ = {};
		}

		uint8_t index() const {
			return index_;
		}

	private:
		uint8_t index_;
		PalettedPixelAccessor contents_;

		size_t x_ = 0, y_ = 0;
		bool continuous_ = true;
		std::optional<SpriteEvent> enqueued_;

		enum class OutputStrategy {
			IX, HL,
		} strategy;

		std::optional<uint8_t> pixels_at(size_t x, size_t y) {
			const auto left = contents_.pixel(x, y);
			const auto right = contents_.pixel(x + 1, y);

			if(PalettedPixelAccessor::is_transparent(left) && PalettedPixelAccessor::is_transparent(right)) return {};

			uint8_t value = 0;
			if(!PalettedPixelAccessor::is_transparent(left)) value |= left << 4;
			if(!PalettedPixelAccessor::is_transparent(right)) value |= right;
			return value;
		}
};

