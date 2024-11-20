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
		/// Moves the cursor location by a specified amount.
		Move,
		/// Writes a byte value and advances one position.
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
		enum class Order {
			/// Serialises each row from from left to right and at the end of each row moves down to the one below.
			RowsFirstDownward,
			/// Serialises each column from top to bottom in two-row steps and at the end of each column moves one to the right.
			ColumnsFirstRightward,
			/// Serialises each column from top to bottom in two-row steps and at the end of each column moves one to the left.
			ColumnsFirstLeftward,
		};

		SpriteSerialiser(
			uint8_t index,
			const PixelAccessor &accessor,
			const std::map<uint32_t, uint8_t> &palette,
			Order order) :
				index_(index),
				contents_(accessor, palette, PalettedPixelAccessor::Transformation::None),
				order_(order)
			{
				reset();
			}

		SpriteEvent next() {
			if(enqueued_) {
				const auto result = *enqueued_;
				enqueued_ = {};
				return result;
			}

			std::optional<NextPixels> next;
			switch(order_) {
				case Order::RowsFirstDownward:		next = next_rows_first(); 		break;
				case Order::ColumnsFirstLeftward:
				case Order::ColumnsFirstRightward:
					next = next_columns_first();
				break;
			}

			if(!next) {
				return SpriteEvent{.type = SpriteEvent::Type::Stop};
			}

			const auto sprite_event = SpriteEvent{
				.type = SpriteEvent::Type::OutputByte,
				.content.output = next->value,
			};

			if(next->continuous) {
				return sprite_event;
			}

			continuous_ = true;
			enqueued_ = sprite_event;
			return SpriteEvent{
				.type = SpriteEvent::Type::Move,
				.content.move.x = next->x >> 1,
				.content.move.y = next->y,
			};
		}

		void reset() {
			switch(order_) {
				default:
					x_ = y_ = 0;
				break;
				case Order::ColumnsFirstLeftward:
					y_ = 0;
					x_ = contents_.width() - 2;
				break;
			}
			continuous_ = true;
			enqueued_ = {};
		}

		uint8_t index() const {
			return index_;
		}

	private:
		uint8_t index_;
		PalettedPixelAccessor contents_;
		Order order_;

		size_t x_ = 0, y_ = 0;
		bool continuous_ = true;
		std::optional<SpriteEvent> enqueued_;

		enum class OutputStrategy {
			IX, HL,
		} strategy;

		/// @returns The combined byte consisting of the two subpixels located at (x, y) and (x+1, y) if either is opaque; otherwise std::nullopt.
		std::optional<uint8_t> pixels_at(size_t x, size_t y) {
			const auto left = contents_.pixel(x, y);
			const auto right = contents_.pixel(x + 1, y);

			if(PalettedPixelAccessor::is_transparent(left) && PalettedPixelAccessor::is_transparent(right)) return {};

			uint8_t value = 0;
			if(!PalettedPixelAccessor::is_transparent(left)) value |= left << 4;
			if(!PalettedPixelAccessor::is_transparent(right)) value |= right;
			return value;
		}

		struct NextPixels {
			uint8_t value;
			bool continuous;
			size_t x, y;
		};

		std::optional<NextPixels> next_rows_first() {
			while(y_ < contents_.height()) {
				while(x_ < contents_.width()) {
					const auto x = x_;
					x_ += 2;

					const auto pixels = pixels_at(x, y_);
					if(pixels) {
						return NextPixels {
							.value = *pixels,
							.continuous = continuous_,
							.x = x,
							.y = y_,
						};
					}
					continuous_ = false;
				}

				++y_;
				x_ = 0;
				continuous_ = false;
			}
			return {};
		}

		std::optional<NextPixels> next_columns_first() {
			while(x_ >= 0 && x_ < contents_.width()) {
				while(y_ < contents_.height()) {
					const auto y = y_;
					y_ += 2;

					const auto pixels = pixels_at(x_, y);
					if(pixels) {
						return NextPixels {
							.value = *pixels,
							.continuous = continuous_,
							.x = x_,
							.y = y,
						};
					}
					continuous_ = false;
				}

				// Either jump back to the top and fill in the odd lines,
				// or proceed to the top of the next row.
				if(!(y_ & 1)) {
					y_ = 1;
				} else {
					y_ = 0;
					x_ += (order_ == Order::ColumnsFirstRightward) ? 2 : -2;
				}
				continuous_ = false;
			}
			return {};
		}
};
