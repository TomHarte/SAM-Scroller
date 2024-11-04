//
//  TileSerialiser.h
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 26/10/2024.
//

#pragma once

#include <array>
#include <cstdint>
#include <cmath>

struct TileEvent {
	enum class Type {
		/// Start a new line two lines up from the start of the current line.
		Up2,
		/// Add @c .content to the current output address.
		DownN,
		/// Output the word described by @c .content.
		OutputWord,
		/// Output the byte described by @c content.
		OutputByte,
		/// Stop outputting. Subsequent events will be undefined.
		Stop
	};
	Type type = Type::Stop;
	uint16_t content;
};

template <int TileSize>
struct TileSerialiser {
	std::array<uint8_t, TileSize*TileSize> contents;
	uint8_t index;

	/// Sets the portion of the tile to serialise and resets serialisation.
	///
	/// ...
	/// -2 = remove two columns from the right;
	/// -1 = remove one column from the right;
	/// 0 = serialise whole thing;
	/// 1 = remove left column;
	/// 2 = remove two columns from the left;
	/// ...
	///
	/// One column = one byte's width, i.e. two pixels.
	void set_slice(int slice) {
		odd_width_ = slice & 1;
		words_wide_ = (TileSize >> 2) - ((abs(slice) + 1) >> 1);
		byte_begin_ = (slice >= 0) ? 0 : (-slice << 1);
		reset();
	}

	TileSerialiser() {
		set_slice(0);
	}

	TileEvent next() {
		// See whether advance proceeds from pixels to a line boundary.
		if(x_ == (words_wide_ << 2) + (odd_width_ << 1)) {
			x_ = 0;
			++y_;

			if(y_ == TileSize) {
				return TileEvent{.type = TileEvent::Type::Stop};
			} else if(y_ == TileSize >> 1) {
				previous_.content = 13*128 + (words_wide_ << 1);
				previous_.type = TileEvent::Type::DownN;
			} else if(y_) {
				previous_.type = TileEvent::Type::Up2;
			}
			return previous_;
		}

		// Swizzle address.
		const int base = swizzled_offset() + byte_begin_;

		// If at start of line and with an odd width, send an introductory byte.
		if(!x_ && odd_width_ && previous_.type != TileEvent::Type::OutputByte) {
			previous_.type = TileEvent::Type::OutputByte;
			previous_.content =
				(contents[base + 1] << 4) |
				(contents[base + 0] << 0);
			x_ += 2;
		} else {
			previous_.type = TileEvent::Type::OutputWord;
			previous_.content =
				(contents[base + 1] << 12) |
				(contents[base + 0] << 8) |
				(contents[base + 3] << 4) |
				(contents[base + 2] << 0);
			x_ += 4;
		}

		return previous_;
	}

	void reset() {
		x_ = 0;
		y_ = 0;
		previous_ = TileEvent{};
	}

	private:
		int swizzled_offset() {
			return x_ + ((y_ & ~8) * 32) + ((y_ >> 3) * 16);
		}

		int x_, y_;
		int odd_width_;
		int byte_begin_;
		int words_wide_;

		TileEvent previous_;
};
