//
//  PixelAccessor.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 26/10/2024.
//

#pragma once

#include <map>
#include <vector>

/*!
	Wraps the necessary CGBitmap calls to take an NSImage and provide
	access to its pixel contents.
*/
class PixelAccessor {
	public:
		PixelAccessor(NSImage *image) {
			colour_space_ = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
			bitmap_ = CGBitmapContextCreate(
				NULL,
				image.size.width, image.size.height,
				8, 0,
				colour_space_, kCGImageAlphaPremultipliedLast);
			NSGraphicsContext *gctx = [NSGraphicsContext graphicsContextWithCGContext:bitmap_ flipped:NO];
			[NSGraphicsContext setCurrentContext:gctx];
			[image drawInRect:NSMakeRect(0, 0, image.size.width, image.size.height)];

			pixels_ = reinterpret_cast<uint8_t *>(CGBitmapContextGetData(bitmap_));
			width_ = CGBitmapContextGetWidth(bitmap_);
			height_ = CGBitmapContextGetHeight(bitmap_);
			bytes_per_row_ = CGBitmapContextGetBytesPerRow(bitmap_);
		}

		~PixelAccessor() {
			[NSGraphicsContext setCurrentContext:nil];
			CGContextRelease(bitmap_);
			CGColorSpaceRelease(colour_space_);
		}

		size_t bytes_per_row() const { return bytes_per_row_; }
		size_t width() const { return width_; }
		size_t height() const { return height_; }
		uint32_t pixel(size_t x, size_t y) const { return *pixels(x, y); }

		// The following isn't const correct because it's used by Objective-C
		// to call into other C functions, which have no corresponding concept.
		uint32_t *pixels(size_t x, size_t y) {
			return reinterpret_cast<uint32_t *>(&pixels_[y*bytes_per_row_ + x*4]);
		}
		const uint32_t *pixels(size_t x, size_t y) const {
			return reinterpret_cast<uint32_t *>(&pixels_[y*bytes_per_row_ + x*4]);
		}

		static constexpr bool is_transparent(uint32_t colour) {
			return !(colour >> 24);
		}

	private:
		size_t width_, height_;
		CGContextRef bitmap_;
		CGColorSpaceRef colour_space_;
		uint8_t *pixels_;
		size_t bytes_per_row_;
};

/*!
	Takes a PixelAccessor and an active palette, maps it through the paltte and subsequently
	provides pixels as 4bpp palette entries.
*/
class PalettedPixelAccessor {
	public:
		enum class Transformation {
			/// Pixels map exactly to their original locations.
			None,
			/// The image is mirrored across y, so the pixel returned by an (x, y) is that which was at (width - 1 - x, y) in the source image.
			ReverseX,
		};

		PalettedPixelAccessor(
			const PixelAccessor &accessor,
			const std::unordered_map<uint32_t, uint8_t> &palette,
			Transformation transformation
		) :
			width_(accessor.width()),
			height_(accessor.height())
		{
			pixels_.resize(width_ * height_);

			for(size_t y = 0; y < height_; y++) {
				for(size_t x = 0; x < width_; x++) {
					size_t destination = y * width_ + x;
					if(transformation == Transformation::ReverseX) {
						destination = pixels_.size() - 1 - destination;
					}

					const uint32_t source_colour = accessor.pixel(x, y);
					if(PixelAccessor::is_transparent(source_colour)) {
						pixels_[destination] = 0xff;
					} else {
						const auto iterator = palette.find(source_colour);
						pixels_[destination] = iterator != palette.end() ? iterator->second : 0;
					}
				}
			}
		}

		size_t width() const { return width_; }
		size_t height() const { return height_; }
		uint8_t pixel(size_t x, size_t y) const { return *pixels(x, y); }
		const uint8_t *pixels(size_t x, size_t y) const { return &pixels_[y * width_ + x]; }

		static constexpr bool is_transparent(uint8_t colour) {
			return colour == 0xff;
		}

	private:
		size_t width_, height_;
		std::vector<uint8_t> pixels_;
};
