//
//  PixelAccessor.h
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 26/10/2024.
//

#pragma once

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

		size_t bytes_per_row() { return bytes_per_row_; }
		size_t width() { return width_; }
		size_t height() { return height_; }
		uint32_t *pixels(size_t x, size_t y) { return reinterpret_cast<uint32_t *>(&pixels_[y*bytes_per_row_ + x*4]); }
		uint32_t pixel(size_t x, size_t y) { return *pixels(x, y); }

	private:
		size_t width_, height_;
		CGContextRef bitmap_;
		CGColorSpaceRef colour_space_;
		uint8_t *pixels_;
		size_t bytes_per_row_;
};
