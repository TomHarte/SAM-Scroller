//
//  PixelAccessor.h
//  Gryzor Preprocessor
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
			colour_space_ = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
			bitmap_ = CGBitmapContextCreate(
				NULL,
				image.size.width, image.size.height,
				8, 0,
				colour_space_, kCGImageAlphaPremultipliedLast);
			NSGraphicsContext *gctx = [NSGraphicsContext graphicsContextWithCGContext:bitmap_ flipped:NO];
			[NSGraphicsContext setCurrentContext:gctx];
			[image drawInRect:NSMakeRect(0, 0, image.size.width, image.size.height)];

			width_ = CGBitmapContextGetWidth(bitmap_);
			height_ = CGBitmapContextGetHeight(bitmap_);
			bytes_per_row_ = CGBitmapContextGetBytesPerRow(bitmap_);
			pixels_ = reinterpret_cast<uint8_t *>(CGBitmapContextGetData(bitmap_));
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
		PalettedPixelAccessor(const PixelAccessor &accessor, const std::map<uint32_t, uint8_t> &palette) :
			width_(accessor.width()),
			height_(accessor.height())
		{
			pixels_.resize(width_ * height_);
			for(size_t y = 0; y < height_; y++) {
				for(size_t x = 0; x < width_; x++) {
					const uint32_t source_colour = accessor.pixel(x, y);
					const auto iterator = palette.find(source_colour);
					pixels_[y * width_ + x] = iterator != palette.end() ? iterator->second : 0;
				}
			}
		}

		size_t width() const { return width_; }
		size_t height() const { return height_; }
		uint32_t pixel(size_t x, size_t y) const { return pixels_[y * width_ + x]; }

	private:
		size_t width_, height_;
		std::vector<uint8_t> pixels_;
};
