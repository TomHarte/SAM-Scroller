//
//  AppDelegate.m
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 18/10/2024.
//

#import "AppDelegate.h"

#include "PixelAccessor.h"
#include "TileSerialiser.h"
#include "RangeAllocator.h"
#include "RegisterAllocator.h"
#include "SpriteSerialiser.h"

#include <array>
#include <bit>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

static constexpr int TileSize = 16;

@class DraggableTextField;
@protocol DraggableTextFieldFileDelegate
- (void)draggableTextField:(DraggableTextField *)field didReceiveFile:(NSURL *)url;
@end

@interface DraggableTextField: NSTextField <NSDraggingDestination>
@property (nonatomic, weak) id<DraggableTextFieldFileDelegate> fileDelegate;
@end

@implementation DraggableTextField

- (void)awakeFromNib {
	[super awakeFromNib];
	[self registerForDraggedTypes:@[(__bridge NSString *)kUTTypeFileURL]];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
	return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
	for(NSPasteboardItem *item in [[sender draggingPasteboard] pasteboardItems]) {
		NSURL *URL = [NSURL URLWithString:[item stringForType:(__bridge NSString *)kUTTypeFileURL]];
		[self.fileDelegate draggableTextField:self didReceiveFile:URL];
	}
	return YES;
}

@end


@interface AppDelegate () <DraggableTextFieldFileDelegate>
@property (strong) IBOutlet NSWindow *window;
@property (weak) IBOutlet NSTextField *workFolderTextField;
@property (weak) IBOutlet DraggableTextField *extractTilesView;
@property (weak) IBOutlet DraggableTextField *convertMapView;
@end

@implementation AppDelegate {
	NSString *_workFolder;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
	_workFolder = [[NSUserDefaults standardUserDefaults] valueForKey:@"workFolder"];
	[self.workFolderTextField setStringValue:_workFolder];
	self.extractTilesView.fileDelegate = self;
	self.convertMapView.fileDelegate = self;
}

- (IBAction)changeWorkFolder:(id)sender {
	NSOpenPanel *panel = [[NSOpenPanel alloc] init];
	panel.canChooseFiles = NO;
	panel.canChooseDirectories = YES;
	panel.allowsMultipleSelection = NO;
	[panel runModal];
	_workFolder = [NSString stringWithUTF8String:panel.URL.fileSystemRepresentation];
	[self.workFolderTextField setStringValue:_workFolder];
	[[NSUserDefaults standardUserDefaults] setValue:_workFolder forKey:@"workFolder"];
}

- (IBAction)generateFixedCode:(id)sender {
	[self writeColumnFunctions:_workFolder];
}

- (IBAction)convertAssets:(id)sender {
	[self encode:_workFolder];
}

- (void)draggableTextField:(DraggableTextField *)field didReceiveFile:(NSURL *)url {
	if(field == self.extractTilesView) {
		NSImage *source = [[NSImage alloc] initWithContentsOfURL:url];
		[self dissect:source destination:_workFolder];
		NSLog(@"OVER-DISSECTING");
	} else {
		NSLog(@"NOT YET IMPLEMENTED");
	}
}

// MARK: - Conversion functions.


- (NSString *)loadRegister:(char)reg previous:(uint8_t)previous target:(uint8_t)target {
	if(previous == target) {
		return @"";
	}

	if(target == ((previous + 1) & 0xff)) {
		return [NSString stringWithFormat:@"\t\tinc %c\n", reg];
	}

	if(target == ((previous - 1) & 0xff)) {
		return [NSString stringWithFormat:@"\t\tdec %c\n", reg];
	}

	return [NSString stringWithFormat:@"\t\tld %c, 0x%02x\n", reg, target];
}

- (NSString *)loadPair:(const char *)pair previous:(std::optional<uint16_t>)previous target:(uint16_t)target {
	if(previous) {
		if(*previous == target){
			return @"";
		}

		if((*previous&0xff00) == (target&0xff00)) {
			return [self loadRegister:pair[1] previous:*previous & 0x00ff target:target & 0x00ff];
		}

		if((*previous&0x00ff) == (target&0x00ff)) {
			return [self loadRegister:pair[0] previous:*previous >> 8 target:target >> 8];
		}
	}

	return [NSString stringWithFormat:@"\t\tld %s, 0x%04x\n", pair, target];
}

- (void)dissect:(NSImage *)image destination:(NSString *)directory {
	PixelAccessor accessor(image);
	std::map<uint32_t, uint8_t> colours;

	using Tile = std::array<uint32_t, TileSize*TileSize>;
	std::map<Tile, int> tiles;

	using Column = std::array<uint8_t, 12>;
	std::vector<Column> columns;

	// Select vertical range.
	const int bottom = static_cast<int>(accessor.height());
	const int top = bottom - 192;

	// Find unique tiles in that range, populating the tile map.
	for(int x = 0; x < accessor.width(); x += TileSize) {
		Column &column = columns.emplace_back();
		for(int y = top; y < bottom; y += TileSize) {
			Tile tile;
			for(int ty = 0; ty < TileSize; ty++) {
				const uint32_t *const row = accessor.pixels(0, y + ty);
				for(int tx = 0; tx < TileSize; tx++) {
					tile[ty*TileSize + tx] = row[x + tx];
				}
			}

			auto [it, is_new] = tiles.try_emplace(tile, tiles.size());
			if(is_new) {
				// Write file.
				uint8_t *planes[] = { reinterpret_cast<uint8_t *>(accessor.pixels(x, y)) };
				NSBitmapImageRep *image_representation =
					[[NSBitmapImageRep alloc]
						initWithBitmapDataPlanes:planes
						pixelsWide:TileSize
						pixelsHigh:TileSize
						bitsPerSample:8
						samplesPerPixel:4
						hasAlpha:YES
						isPlanar:NO
						colorSpaceName:NSDeviceRGBColorSpace
						bytesPerRow:accessor.bytes_per_row()
						bitsPerPixel:0];

				NSData *const data = [image_representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
				NSString *const name = [directory stringByAppendingPathComponent:[NSString stringWithFormat:@"tiles/%d.png", it->second]];
				[data
					writeToFile:name
					atomically:NO];
			}
			column[(y - top) / TileSize] = it->second << 1;
		}
	}


	NSMutableString *map = [[NSMutableString alloc] init];
	[map appendFormat:@"\tmap:\n"];
	for(auto &column : columns) {
		[map appendFormat:@"\t\tdb 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
			column[0], column[1], column[2], column[3], column[4], column[5], column[6], column[7], column[8], column[9], column[10], column[11]];
	}

	[map appendFormat:@"\n\tdiffs:\n"];
	auto c_it = columns.begin() + 1;
	while(c_it != columns.end()) {
		auto c_before = c_it - 1;

		const int diffs = [&]() {
			int total = 0;
			for(int c = 0; c < 12; c++) {
				total += (*c_it)[c] != (*c_before)[c];
			}
			return total;
		}();

		[map appendFormat:@"\t\tdb 0x%02x, 0x%02x, 0x%02x\t; %d total\n",
			((*c_it)[3] != (*c_before)[3] ? 0x02 : 0x0) |
			((*c_it)[2] != (*c_before)[2] ? 0x04 : 0x0) |
			((*c_it)[1] != (*c_before)[1] ? 0x08 : 0x0) |
			((*c_it)[0] != (*c_before)[0] ? 0x10 : 0x0),

			((*c_it)[7] != (*c_before)[7] ? 0x02 : 0x0) |
			((*c_it)[6] != (*c_before)[6] ? 0x04 : 0x0) |
			((*c_it)[5] != (*c_before)[5] ? 0x08 : 0x0) |
			((*c_it)[4] != (*c_before)[4] ? 0x10 : 0x0),

			((*c_it)[11] != (*c_before)[11] ? 0x02 : 0x0) |
			((*c_it)[10] != (*c_before)[10] ? 0x04 : 0x0) |
			((*c_it)[9] != (*c_before)[9] ? 0x08 : 0x0) |
			((*c_it)[8] != (*c_before)[8] ? 0x10 : 0x0),

			diffs
		];

		++c_it;
	}

	NSString *const map_name = [directory stringByAppendingPathComponent:@"map.z80s"];
	[map writeToFile:map_name atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

- (NSString *)tiles:(NSString *)name slice:(int)slice source:(std::vector<TileSerialiser<TileSize>> &)tiles page:(int)page {
	NSMutableString *code = [[NSMutableString alloc] init];

	[code appendFormat:@"ds align 256\n"];
	[code appendFormat:@"\ttiles_%@_page: EQU %d + 0b00100000\n", name, page];
	[code appendFormat:@"\ttiles_%@:", name];
	bool is_first = true;
	for(size_t c = 0; c < tiles.size(); c++) {
		if(!(c&3)) {
			[code appendString:@"\n\t\tdw "];
			is_first = true;
		}
		if(!is_first) [code appendString:@", "];
		[code appendFormat:@"@+%@_%d", name, int(c)];
		is_first = false;
	}
	[code appendString:@"\n\n"];

	for(auto &tile: tiles) {
		tile.set_slice(slice);
		RegisterAllocator<TileSize> allocator(tile);

		[code appendFormat:@"\t@%@_%d:\n", name, tile.index()];
		[code appendString:@"\t\tld (@+return+1), de\n"];
		[code appendString:@"\t\tld sp, hl\n\n"];

		bool finished = false;
		while(!finished) {
			auto event = tile.next();
			switch(event.type) {
				case TileEvent::Type::Stop:	finished = true;	break;

				case TileEvent::Type::Up2:
					[code appendString:@"\t\tdec h\n"];
					[code appendString:@"\t\tld sp, hl\n\n"];
				break;
				case TileEvent::Type::DownN:
					[code appendFormat:@"\t\tld hl, %d\n", event.content];
					[code appendString:@"\t\tadd hl, sp\n"];
					[code appendString:@"\t\tld sp, hl\n\n"];
				break;

				case TileEvent::Type::OutputWord: {
					const auto action = allocator.next_word(event.content);
					switch(action.type) {
						case RegisterEvent::Type::Load:
							[code appendString:
								[self loadPair:action.load_register() previous:action.previous_value target:action.value]
							];
							[[fallthrough]];
						case RegisterEvent::Type::Reuse:
							[code appendFormat:@"\t\tpush %s\n", action.push_register()];
						break;

						case RegisterEvent::Type::UseConstant:
							throw 0;	// Impossible.
						break;
					}
				} break;
				case TileEvent::Type::OutputByte: {
					const auto action = allocator.next_byte(event.content);
					switch(action.type) {
						case RegisterEvent::Type::Load:
							[code appendFormat:@"\t\tld %s, 0x%02x\n", action.load_register(), action.value];
							[[fallthrough]];
						case RegisterEvent::Type::Reuse:
							[code appendFormat:@"\t\tld (hl), %s\n", action.load_register()];
						break;

						case RegisterEvent::Type::UseConstant:
							[code appendFormat:@"\t\tld (hl), 0x%02x\n", action.value];
						break;
					}
				} break;
			}
		}

		[code appendFormat:@"\t@return:\n"];
		[code appendString:@"\t\tjp 1234\n\n"];
	}

	return code;
}

- (NSArray<NSString *> *)files:(NSArray<NSString *> *)files withPrefix:(NSString *)prefix {
	NSMutableArray<NSString *> *result = [[NSMutableArray alloc] init];
	[files enumerateObjectsUsingBlock:^(NSString *name, NSUInteger, BOOL *) {
		[result addObject:[prefix stringByAppendingPathComponent:name]];
	}];
	return result;
}

- (NSArray<NSString *> *)imageFiles:(NSString *)directory {
	return
		[self files:
			[[[NSFileManager defaultManager]
				contentsOfDirectoryAtPath:directory error:nil]
				filteredArrayUsingPredicate:
					[NSPredicate predicateWithBlock:^BOOL(NSString *string, NSDictionary<NSString *,id> *) {
						return [string hasSuffix:@"png"];
					}]]
			withPrefix:directory];
}

- (NSArray<NSString *> *)tileFiles:(NSString *)directory {
	return [self imageFiles:[directory stringByAppendingPathComponent:@"tiles"]];
}

- (NSArray<NSString *> *)spriteFiles:(NSString *)directory {
	return [self imageFiles:[directory stringByAppendingPathComponent:@"sprites"]];
}

- (void)encode:(NSString *)directory {
	// Get list of all PNGs.
	NSArray<NSString *> *tile_files = [self tileFiles:directory];
	NSArray<NSString *> *sprite_files = [self spriteFiles:directory];

	// Build palette based on tiels and sprites.
	std::map<uint32_t, uint8_t> palette;
	for(NSString *file in [tile_files arrayByAddingObjectsFromArray:sprite_files]) {
		// Tiles: grab all included colours.
		NSData *fileData = [NSData dataWithContentsOfFile:file];
		PixelAccessor accessor([[NSImage alloc] initWithData:fileData]);
		for(size_t y = 0; y < accessor.height(); y++) {
			for(size_t x = 0; x < accessor.width(); x++) {
				const uint8_t palette_index = static_cast<uint8_t>(palette.size());

				// TODO: map to Sam palette here, so that multiple different input RGBs that map to the same
				// thing on the Sam don't get unique palette locations.
				//
				// (or, possibly, defer to a palette reduction step?)

				// Quick hack! Just don't allow more than 15 colours. Overflow will compete.
				const auto colour = accessor.pixel(x, y);
				if(!PixelAccessor::is_transparent(colour)) {
					palette.try_emplace(colour, std::min(palette_index, uint8_t(15)));
				}
			}
		}
	}

	// Prepare lists of tiles and sprites for future dicing and writing.
	std::vector<TileSerialiser<TileSize>> tiles;
	for(NSString *file in tile_files) {
		NSData *fileData = [NSData dataWithContentsOfFile:file];
		PixelAccessor accessor([[NSImage alloc] initWithData:fileData]);
		tiles.emplace_back(
			[[file lastPathComponent] intValue],
			accessor,
			palette);
	}

	std::vector<SpriteSerialiser> sprites;
	for(NSString *file in sprite_files) {
		NSData *fileData = [NSData dataWithContentsOfFile:file];
		PixelAccessor accessor([[NSImage alloc] initWithData:fileData]);
		sprites.emplace_back(
			[[file lastPathComponent] intValue],
			accessor,
			palette);
	}

	// Write palette, in Sam format.
	[self writePalette:palette file:[directory stringByAppendingPathComponent:@"palette.z80s"]];

	// Compile all
	[self compileTiles:tiles directory:directory];
	[self compileSprites:sprites directory:directory];
}

- (void)compileTiles:(std::vector<TileSerialiser<TileSize>> &)tiles directory:(NSString *)directory {
	NSMutableString *code = [[NSMutableString alloc] init];
	[code appendString:@"\t; The following tile outputters are automatically generated.\n"];
	[code appendString:@"\t;\n"];
	[code appendString:@"\t; Input:\n"];
	[code appendString:@"\t;	* for tiles that are an even number of byes wide, HL points to one after the lower right corner of the output location;\n"];
	[code appendString:@"\t;	* for tiles that are an odd number of bytes wide, HL points to the lower right corner of the output location;\n"];
	[code appendString:@"\t;	* DE is a link register, indicating where the function should return to.\n"];
	[code appendString:@"\t;\n"];
	[code appendString:@"\t; Rules:\n"];
	[code appendString:@"\t;	* IX and IY should be preserved.\n"];
	[code appendString:@"\t;\n"];
	[code appendString:@"\t; Output:\n"];
	[code appendString:@"\t;	* HL will be 15 lines earlier than it was at input.\n"];
	[code appendString:@"\t; i.e. if stacking tiles from bottom to top, the caller will need to subtract a\n"];
	[code appendString:@"\t; further 128 from HL before calling the next outputter.\n"];
	[code appendString:@"\t;\n\n"];

	[code appendString:@"ORG 0\nDUMP 16, 0\n"];
	[code appendString:[self tiles:@"full" slice:0 source:tiles page:16]];
	[code appendString:[self tiles:@"left_7" slice:-1 source:tiles page:16]];
	[code appendString:[self tiles:@"right_1" slice:7 source:tiles page:16]];

	[code appendString:@"ORG 0\nDUMP 18, 0\n"];
	[code appendString:[self tiles:@"left_6" slice:-2 source:tiles page:18]];
	[code appendString:[self tiles:@"right_2" slice:6 source:tiles page:18]];
	[code appendString:[self tiles:@"left_5" slice:-3 source:tiles page:18]];
	[code appendString:[self tiles:@"right_3" slice:5 source:tiles page:18]];

	[code appendString:@"ORG 0\nDUMP 20, 0\n"];
	[code appendString:[self tiles:@"left_4" slice:-4 source:tiles page:20]];
	[code appendString:[self tiles:@"right_4" slice:4 source:tiles page:20]];
	[code appendString:[self tiles:@"left_3" slice:-5 source:tiles page:20]];
	[code appendString:[self tiles:@"right_5" slice:3 source:tiles page:20]];

	[code appendString:@"ORG 0\nDUMP 22, 0\n"];
	[code appendString:[self tiles:@"left_2" slice:-6 source:tiles page:22]];
	[code appendString:[self tiles:@"right_6" slice:2 source:tiles page:22]];
	[code appendString:[self tiles:@"left_1" slice:-7 source:tiles page:22]];
	[code appendString:[self tiles:@"right_7" slice:1 source:tiles page:22]];

	[code writeToFile:[directory stringByAppendingPathComponent:@"tiles.z80s"] atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

- (void)compileSprites:(std::vector<SpriteSerialiser> &)sprites directory:(NSString *)directory {
	// TODO: be MUCH less dense than this, document precepts.
	NSMutableString *code = [[NSMutableString alloc] init];

	for(auto &sprite: sprites) {
		[code appendFormat:@"\tsprite_%d:\n", sprite.index()];
		[code appendString:@"\t\tld (@+return+1), de\n\n"];
		std::optional<uint16_t> bc;

		// Grab allocated register ranges.
		static constexpr size_t NumRegisters = 3;
		static constexpr char RegisterNames[3] = {'a', 'd', 'e'};

		RangeAllocator<uint8_t> register_allocator(NumRegisters);
		sprite.reset();
		int time = 0;
		while(true) {
			const auto event = sprite.next();
			if(event.type == SpriteEvent::Type::Stop) {
				break;
			}

			if(event.type == SpriteEvent::Type::OutputByte) {
				register_allocator.add_value(time, event.content.output);
			}

			++time;
		}

		const auto allocations = register_allocator.spans();
		auto next_allocation = allocations.begin();
		std::optional<uint8_t> registers[NumRegisters];

		bool moved = true;
		uint16_t hl = 0;
		time = 0;
		sprite.reset();
		while(true) {
			const auto event = sprite.next();
			if(event.type == SpriteEvent::Type::Stop) {
				break;
			}
			
			// Apply a new allocation if one pops into existence here.
			if(next_allocation != allocations.end() && next_allocation->time == time) {
				registers[next_allocation->reg] = next_allocation->value;
				[code appendFormat:@"\t\tld %c, 0x%02x\n", RegisterNames[next_allocation->reg], next_allocation->value];
				++next_allocation;
			}

			if(event.type == SpriteEvent::Type::Move) {
				moved = true;
				const uint16_t target = (event.content.move.y * 128) + event.content.move.x;
				const uint16_t offset = target - hl;
				hl = target;

				[code appendString:[self loadPair:"bc" previous:bc target:offset]];
				bc = offset;
				[code appendString:@"\t\tadd hl, bc\n\n"];
			} else {
				if(!moved) {
					[code appendString:@"\t\tinc l\n"];
					++hl;
				}
				moved = false;
				
				bool loaded = false;
				for(size_t c = 0; c < NumRegisters; c++) {
					if(registers[c] && event.content.output == registers[c]) {
						loaded = true;
						[code appendFormat:@"\t\tld (hl), %c\n", RegisterNames[c]];
						break;
					}
				}
				if(!loaded) {
					[code appendFormat:@"\t\tld (hl), 0x%02x\n", event.content.output];
				}
			}
			
			++time;
		}

		[code appendFormat:@"\t@return:\n"];
		[code appendString:@"\t\tjp 1234\n\n"];
	}

	[code writeToFile:[directory stringByAppendingPathComponent:@"sprites.z80s"] atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

- (void)writePalette:(std::map<uint32_t, uint8_t> &)palette file:(NSString *)file {
	NSMutableString *encoded = [[NSMutableString alloc] init];
	[encoded appendFormat:@"\tpalette:\n\t\tdb "];

	std::array<uint8_t, 16> values{};
	for(const auto &colour: palette) {
		const uint8_t wide_red = (colour.first >> 0) & 0xff;
		const uint8_t wide_green = (colour.first >> 8) & 0xff;
		const uint8_t wide_blue = (colour.first >> 16) & 0xff;

		const auto remap = [](uint8_t source) {
			return int(roundf(7.0 * static_cast<float>(source) / 255.0));
		};

		const uint8_t red = remap(wide_red);
		const uint8_t green = remap(wide_green);
		const uint8_t blue = remap(wide_blue);

		const uint8_t bright = (red & 1) + (green & 1) + (blue & 1);
		const uint8_t sam_colour =
			((green & 4) ? 0x40 : 0x00) |
			((red & 4) ? 0x20 : 0x00) |
			((blue & 4) ? 0x10 : 0x00) |

			((green & 2) ? 0x08 : 0x00) |
			((red & 2) ? 0x04 : 0x00) |
			((blue & 2) ? 0x02 : 0x00) |

			((bright >= 2) ? 0x01 : 0x00);
		values[colour.second] = sam_colour;
	}

	bool is_first = true;
	for(uint8_t value: values) {
		if(!is_first) [encoded appendString:@", "];
		[encoded appendFormat:@"0x%02x", value];
		is_first = false;
	}

	[encoded appendString:@"\n"];
	[encoded writeToFile:file atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

- (void)writeColumnFunctions:(NSString *)directory {
	NSMutableString *code = [[NSMutableString alloc] init];

	[code appendString:@"\tds align 256\n"];
	[code appendString:@"\tslivers:\n"];
	[code appendString:@"\t\tdw "];
	for(int c = 0; c < 16; c++) {
		if(c) [code appendString:@", "];
		[code appendFormat:@"@+draw_sliver%d", c];
	}
	[code appendString:@"\n\n"];

	for(int c = 0; c < 16; c++) {
		// On input: IX points one beyond the next tile ID.
		// A contains the top byte of the tile dispatch table.
		// DE acts as the link register.

		[code appendFormat:@"\t@draw_sliver%d:\n", c];
		[code appendString:@"\t\tld (@+return + 1), de\n"];

		// Store dispatch table pointer.
		for(int p = 0; p < __builtin_popcount(c); p++) {
			[code appendFormat:@"\t\tld (@+loadslot%d + 3), a\n", p];
		}

		int mask = 1;
		int offset = 0;
		auto append_offset = [&] {
			if(offset) {
				[code appendFormat:@"\t\tld bc, -%d\n", offset];
				[code appendString:@"\t\tadd hl, bc\n"];
			}
			offset = 0;
		};

		int slot = 0;
		while(mask < 16) {
			[code appendString:@"\t\tdec ix\n"];
			if(c & mask) {
				append_offset();
				offset = 128;

				[code appendString:@"\t\tld a, (ix + 0)\n"];
				[code appendFormat:@"\t\tld (@+loadslot%d + 2), a\n", slot];
				[code appendFormat:@"\t@loadslot%d:\n", slot++];
				[code appendString:@"\t\tld de, (1234)\n"];
				[code appendString:@"\t\tld (@+dispatch + 1), de\n"];
				[code appendString:@"\t\tld de, @+end_dispatch\n"];
				[code appendString:@"\t@dispatch:\n"];
				[code appendString:@"\t\tjp 1234\n"];
				[code appendString:@"\t@end_dispatch:\n"];
				[code appendString:@"\n"];
			} else {
				offset += 16*128;
			}

			mask <<= 1;
		}
		append_offset();
		[code appendString:@"\t@return:\n"];
		[code appendString:@"\t\tjp 1234\n\n"];
	}

	[code writeToFile:[directory stringByAppendingPathComponent:@"slivers.z80s"] atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

@end
