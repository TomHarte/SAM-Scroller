//
//  AppDelegate.m
//  Map Preprocessor
//
//  Created by Thomas Harte on 18/10/2024.
//

#import "AppDelegate.h"

#include "PixelAccessor.h"
#include "TileSerialiser.h"
#include "OptionalRegisterAllocator.h"
#include "TileRegisterAllocator.h"
#include "SpriteSerialiser.h"

#include "RegisterSet.h"
#include "Operation.h"
#include "Palettiser.h"

#include <array>
#include <bit>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

static constexpr int TileSize = 16;
const auto SpriteSerialisationOrder = SpriteSerialiser::Order::RowsFirstDownward;

namespace {

NSString *stringify(const std::vector<Operation> &operations) {
	NSMutableString *code = [[NSMutableString alloc] init];

	for(const auto &operation: operations) {
		if(operation.type == Operation::Type::NONE) {
			continue;
		}

		switch(operation.type) {
			case Operation::Type::BLANK_LINE: break;
			case Operation::Type::LABEL: [code appendString:@"\t"];	break;
			default: [code appendString:@"\t\t"];	break;
		}
		[code appendString:operation.text()];
		[code appendString:@"\n"];
	}

	return code;
}

}

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
@property (weak) IBOutlet NSProgressIndicator *progressIndicator;
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

// MARK: - Conversion.
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

				NSData *const data =
					[image_representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
				NSString *const name =
					[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"tiles/%d.png", it->second]];
				[data
					writeToFile:name
					atomically:NO];
			}
			column[(y - top) / TileSize] = it->second << 2;
		}
	}


	NSMutableString *map = [[NSMutableString alloc] init];
	[map appendFormat:@"\tmap:\n"];
	for(auto &column : columns) {
		[map appendFormat:
			@"\t\tdb 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
			column[0], column[1], column[2], column[3],
			column[4], column[5], column[6], column[7],
			column[8], column[9], column[10], column[11]];
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
			((*c_it)[3] != (*c_before)[3] ? 0x04 : 0x0) |
			((*c_it)[2] != (*c_before)[2] ? 0x08 : 0x0) |
			((*c_it)[1] != (*c_before)[1] ? 0x10 : 0x0) |
			((*c_it)[0] != (*c_before)[0] ? 0x20 : 0x0),

			((*c_it)[7] != (*c_before)[7] ? 0x04 : 0x0) |
			((*c_it)[6] != (*c_before)[6] ? 0x08 : 0x0) |
			((*c_it)[5] != (*c_before)[5] ? 0x10 : 0x0) |
			((*c_it)[4] != (*c_before)[4] ? 0x20 : 0x0),

			((*c_it)[11] != (*c_before)[11] ? 0x04 : 0x0) |
			((*c_it)[10] != (*c_before)[10] ? 0x08 : 0x0) |
			((*c_it)[9] != (*c_before)[9] ? 0x10 : 0x0) |
			((*c_it)[8] != (*c_before)[8] ? 0x20 : 0x0),

			diffs
		];

		++c_it;
	}

	NSString *const map_name = [directory stringByAppendingPathComponent:@"map.z80s"];
	[map writeToFile:map_name atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

- (NSString *)tileDeclarationPairLeft:(NSString *)left right:(NSString *)right count:(size_t)count page:(int)page {
	NSMutableString *code = [[NSMutableString alloc] init];
	for(NSString *name in @[right, left]) {
		[code appendFormat:@"\tds align 256\n"];
		NSString *set_name = name;
		if(name.length) {
			[code appendFormat:@"\ttiles_%@_page: EQU %d + 0b00100000\n", name, page];
			[code appendFormat:@"\ttiles_%@:\n", name];
		} else {
			set_name = right;
		}
		for(size_t c = 0; c < count; c++) {
			if(c) {
				[code appendString:@"\t\tnop\n"];
			}
			[code appendFormat:@"\t\tjp @+%@_%d\n", set_name, int(c)];
		}
		[code appendString:@"\n\n"];
	}
	return code;
}

- (NSString *)
	tiles:(NSString *)name
	slice:(int)slice
	source:(std::vector<TileSerialiser<TileSize>> &)tiles
	page:(int)page
{
	NSMutableString *code = [[NSMutableString alloc] init];
	for(auto &tile: tiles) {
		tile.set_slice(slice);

		// Two trials are performed; one with IX (and appropriate logic to preserve it across the call) and one without.
		// Whichever ends up with the lowest cost wins.
		std::vector<Operation> operations;

		for(int c = 0; c < 2; c++) {
			TileRegisterAllocator<TileSize> allocator(tile, c & 1);

			std::vector<Operation> trial;
			trial.push_back(Operation::label([[NSString stringWithFormat:@"@%@_%d", name, tile.index()] UTF8String]));
			trial.push_back(Operation::ld(Operand::label_indirect("@+return+1"), Operand::direct(Register::Name::DE)));
			if(c & 1) {
				trial.push_back(
					Operation::ld(
						Operand::label_indirect("@+reload_ix+2"),
						Operand::direct(Register::Name::IX)
					)
				);
			}
			trial.push_back(Operation::ld(Register::Name::SP, Register::Name::HL));

			bool finished = false;
			RegisterSet set;
			int stack_count = 0;
			while(!finished) {
				auto event = tile.next();
				switch(event.type) {
					case TileEvent::Type::Stop:	finished = true;	break;

					case TileEvent::Type::Up2:
						trial.push_back(Operation::unary(Operation::Type::DEC, Register::Name::H));
						trial.push_back(Operation::ld(Register::Name::SP, Register::Name::HL));
						trial.push_back(Operation::nullary(Operation::Type::BLANK_LINE));
						stack_count = 0;
					break;
					case TileEvent::Type::Down2:
						trial.push_back(Operation::unary(Operation::Type::INC, Register::Name::H));
						trial.push_back(Operation::ld(Register::Name::SP, Register::Name::HL));
						trial.push_back(Operation::nullary(Operation::Type::BLANK_LINE));
						stack_count = 0;
					break;
					case TileEvent::Type::Up1: {
						const bool might_be_at_screen_edge = !(slice&1) && (slice <= 0);
						if(might_be_at_screen_edge) {
							trial.push_back(Operation::unary(Operation::Type::DEC, Register::Name::HL));
							trial.push_back(Operation::unary(Operation::Type::RES7, Register::Name::L));
							trial.push_back(Operation::unary(Operation::Type::INC, Register::Name::L));
						} else {
							trial.push_back(Operation::unary(Operation::Type::RES7, Register::Name::L));
						}
						// To consider: if an extra 8kb is available for a duplicate set of the full-size tiles,
						// use those for everywhere except the rightmost column and implement them as `res 7`.

						trial.push_back(Operation::ld(Register::Name::SP, Register::Name::HL));
						trial.push_back(Operation::nullary(Operation::Type::BLANK_LINE));
						stack_count = 0;
					} break;

					case TileEvent::Type::OutputWord: {
						stack_count += 2;
						const auto action = allocator.next_word(tile.event_offset(), event.content);
						switch(action.type) {
							case RegisterEvent::Type::Load:
								trial.push_back(set.load(action.reg, action.value));
								[[fallthrough]];
							case RegisterEvent::Type::Reuse:
								trial.push_back(Operation::unary(Operation::Type::PUSH, Register::pair(action.reg)));
							break;

							case RegisterEvent::Type::UseConstant:
								throw 0;	// Impossible.
							break;
						}
					} break;
					case TileEvent::Type::OutputByte: {
						const auto action = allocator.next_byte(tile.event_offset(), event.content);
						switch(action.type) {
							case RegisterEvent::Type::Load:
								trial.push_back(set.load(action.reg, action.value));
								[[fallthrough]];
							case RegisterEvent::Type::Reuse:
								trial.push_back(
									Operation::ld(
										Operand::indirect(Register::Name::HL),
										Operand::direct(action.reg)
									)
								);
							break;

							case RegisterEvent::Type::UseConstant:
								trial.push_back(
									Operation::ld(
										Operand::indirect(Register::Name::HL),
										Operand::immediate<uint8_t>(action.value)
									)
								);
							break;
						}
					} break;
				}
			}

			if(c & 1) {
				trial.push_back(Operation::label("@reload_ix"));
				trial.push_back(
					Operation::ld(
						Operand::direct(Register::Name::IX),
						Operand::immediate<uint16_t>(0x1234)
					)
				);
			}
			trial.push_back(Operation::label("@return"));
			trial.push_back(Operation::jp(0x1234));
			trial.push_back(Operation::nullary(Operation::Type::BLANK_LINE));

			if(operations.empty() || cost(trial) < cost(operations)) {
				operations = trial;
			}
		}

		[code appendString:stringify(operations)];
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

	// Build palette based on tiles and sprites.
	Palettiser palettiser(4);	// TODO: super-hack here; I'm supplying a rotation I picked to make sure that
								// colour 0 is the background one. That needs to be automated.
	for(NSString *file in [tile_files arrayByAddingObjectsFromArray:sprite_files]) {
		NSData *fileData = [NSData dataWithContentsOfFile:file];
		const PixelAccessor accessor([[NSImage alloc] initWithData:fileData]);
		palettiser.add_colours(accessor);
	}
	const auto palette = palettiser.palette();

	// Prepare lists of tiles and sprites for future dicing and writing.
	std::vector<TileSerialiser<TileSize>> tiles;
	for(NSString *file in tile_files) {
		NSData *fileData = [NSData dataWithContentsOfFile:file];
		PixelAccessor accessor([[NSImage alloc] initWithData:fileData]);
		tiles.emplace_back(
			[[file lastPathComponent] intValue],
			accessor,
			palette.source_mapping);
	}

	std::vector<SpriteSerialiser> sprites;
	for(NSString *file in sprite_files) {
		NSData *fileData = [NSData dataWithContentsOfFile:file];
		PixelAccessor accessor([[NSImage alloc] initWithData:fileData]);
		sprites.emplace_back(
			[[file lastPathComponent] intValue],
			accessor,
			palette.source_mapping,
			SpriteSerialisationOrder);
	}

	// Write palette, in Sam format.
	[self writePalette:palette.sam_palette file:[directory stringByAppendingPathComponent:@"palette.z80s"]];

	// Compile all.
	[self compileSprites:sprites directory:directory];
	[self compileTiles:tiles directory:directory];
}

- (void)compileTiles:(std::vector<TileSerialiser<TileSize>> &)tiles directory:(NSString *)directory {
	NSMutableString *code = [[NSMutableString alloc] init];
	[code appendString:
		@"\t; The following tile outputters are automatically generated.\n"
		@"\t;\n"
		@"\t; Input:\n"
		@"\t;	* for tiles that are an even number of byes wide, HL points to one after the lower right corner of the "
				@"output location;\n"
		@"\t;	* for tiles that are an odd number of bytes wide, HL points to the lower right corner of the output "
				@"location;\n"
		@"\t;	* DE is a link register, indicating where the function should return to.\n"
		@"\t;\n"
		@"\t; Rules:\n"
		@"\t;	* IX should be preserved; and\n"
		@"\t;	* SP is overtly available for any use the outputter prefers.\n"
		@"\t;\n"
		@"\t; At exit:\n"
		@"\t;	* HL will be 1 line earlier than it was at input.\n"
		@"\t; i.e. if stacking tiles from bottom to top, the caller will need to subtract a\n"
		@"\t; further 15*128 from HL before calling the next outputter.\n"
		@"\t;\n"
		@"\t; Each set of tiles is preceded by a long sequence of JP statements that jump to each tile in turn;\n"
		@"\t; this is the means by which dynamic branching happens elsewhere — the map is stored as the low byte\n"
		@"\t; of JP that branches into the tile to be drawn. Although slightly circuitous, this proved to be the\n"
		@"\t; fastest way of implementing that step subject to the bounds of my imagination.\n"
		@"\t;\n\n"
	];

	const auto post = [&](NSString *tiles) {
//		dispatch_sync(dispatch_get_main_queue(), ^{
//			self.progressIndicator.doubleValue += 100.0 / 8.0;
			[code appendString:tiles];
//		});
	};

	//
	// The following two deliberate take copies of the base tiles because the serialisers are stateful.
	//
	const auto prepare_full = [&post, self](std::vector<TileSerialiser<TileSize>> tiles) {
		NSMutableString *subcode = [[NSMutableString alloc] init];
		[subcode appendString:@"\tORG 0\n\tDUMP 16, 0\n"];
		[subcode appendString:[self tileDeclarationPairLeft:@"" right:@"full" count:tiles.size() page:16]];
		[subcode appendString:[self tiles:@"full" slice:0 source:tiles page:16]];
		post(subcode);
	};

	const auto prepare_sliced = [&post, self](std::vector<TileSerialiser<TileSize>> tiles, int page, int left_size) {
		NSMutableString *subcode = [[NSMutableString alloc] init];
		[subcode appendFormat:@"\tORG 0\n\tDUMP %d, 0\n", page];
		NSString *left = [NSString stringWithFormat:@"left_%d", left_size];
		NSString *right = [NSString stringWithFormat:@"right_%d", 8 - left_size];
		[subcode appendString:[self tileDeclarationPairLeft:left right:right count:tiles.size() page:page]];
		[subcode appendString:[self tiles:left slice:left_size - 8 source:tiles page:17]];
		[subcode appendString:[self tiles:right slice:left_size source:tiles page:17]];
		post(subcode);
	};

//	const auto group = dispatch_group_create();
//	dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		prepare_full(tiles);
//	});

	for(int c = 0; c < 7; c++) {
//		dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
			prepare_sliced(tiles, 17 + c, 7 - c);
//		});
	}

//	self.progressIndicator.doubleValue = 0.0;
//	self.progressIndicator.hidden = NO;
//	dispatch_async(dispatch_get_main_queue(), ^{
//		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
//		dispatch_async(dispatch_get_main_queue(), ^{
			[code
				writeToFile:[directory stringByAppendingPathComponent:@"tiles.z80s"]
				atomically:NO
				encoding:NSUTF8StringEncoding
				error:nil];
			self.progressIndicator.hidden = YES;
//		});
//	});
}

- (void)compileSprites:(std::vector<SpriteSerialiser> &)sprites directory:(NSString *)directory {
	NSMutableString *code = [[NSMutableString alloc] init];

	[code appendString:@"\t; The following sprite outputters are automatically generated. They are intended to\n"];
	[code appendString:@"\t; be CALLed in the ordinary Z80 fashion.\n"];
	[code appendString:@"\t;\n"];
	[code appendString:@"\t; Input:\n"];
	[code appendString:@"\t;	* HL is the screen address of the top-left corner of the sprite.\n"];
	[code appendString:@"\t;\n"];
	[code appendString:@"\t; Each outputter potentially overwrites the contents of all registers.\n"];
	[code appendString:@"\t;\n\n"];

	for(auto &sprite: sprites) {
		std::vector<Operation> operations;
		operations.push_back(Operation::label([[NSString stringWithFormat:@"sprite_%d", sprite.index()] UTF8String]));

		// Obtain register allocations.
		OptionalRegisterAllocator<uint8_t> register_allocator(
			std::vector<Register::Name>{Register::Name::A, Register::Name::D, Register::Name::E}
		);
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

		// Generate code.
		bool moved = true;
		uint16_t hl = 0;
		time = 0;
		sprite.reset();
		RegisterSet set;
		while(true) {
			const auto event = sprite.next();
			if(event.type == SpriteEvent::Type::Stop) {
				break;
			}

			// Apply a new allocation if one pops into existence here.
			if(next_allocation != allocations.end() && next_allocation->time == time) {
				operations.push_back(set.load(next_allocation->reg, next_allocation->value));
				++next_allocation;
			}

			if(event.type == SpriteEvent::Type::Move) {
				moved = true;
				const uint16_t target = (event.content.move.y * 128) + event.content.move.x;
				const uint16_t offset = target - hl;
				hl = target;

				operations.push_back(set.load(Register::Name::BC, offset));
				operations.push_back(Operation::add(Register::Name::HL, Register::Name::BC));
				operations.push_back(Operation::nullary(Operation::Type::BLANK_LINE));
			} else {
				if(!moved) {
					switch(SpriteSerialisationOrder) {
						case SpriteSerialiser::Order::RowsFirstDownward:
							operations.push_back(Operation::unary(Operation::Type::INC, Register::Name::L));
							++hl;
						break;
						case SpriteSerialiser::Order::ColumnsFirstRightward:
						case SpriteSerialiser::Order::ColumnsFirstLeftward:
							operations.push_back(Operation::unary(Operation::Type::INC, Register::Name::H));
							hl += 256;
						break;
					}
				}
				moved = false;

				if(const auto source = set.find(event.content.output); source) {
					operations.push_back(
						Operation::ld(
							Operand::indirect(Register::Name::HL),
							Operand::direct(*source)
						)
					);
				} else {
					operations.push_back(
						Operation::ld(
							Operand::indirect(Register::Name::HL),
							Operand::immediate<uint8_t>(event.content.output)
						)
					);
				}
			}

			++time;
		}

		operations.push_back(Operation::nullary(Operation::Type::RET));
		operations.push_back(Operation::nullary(Operation::Type::BLANK_LINE));

		NSLog(@"%d has cost %zu", sprite.index(), cost(operations));
		[code appendString:stringify(operations)];
	}

	[code
		writeToFile:[directory stringByAppendingPathComponent:@"sprites.z80s"]
		atomically:NO
		encoding:NSUTF8StringEncoding
		error:nil];
}

- (void)writePalette:(const std::vector<uint8_t> &)palette file:(NSString *)file {
	NSMutableString *encoded = [[NSMutableString alloc] init];
	[encoded appendFormat:@"\tpalette:\n\t\tdb "];

	bool is_first = true;
	for(uint8_t value: palette) {
		if(!is_first) [encoded appendString:@", "];
		[encoded appendFormat:@"0x%02x", value];
		is_first = false;
	}

	[encoded appendString:@"\n"];
	[encoded writeToFile:file atomically:NO encoding:NSUTF8StringEncoding error:nil];
}

- (void)writeColumnFunctions:(NSString *)directory {
	NSMutableString *code = [[NSMutableString alloc] init];

	[code appendString:
		@"\t; The following routines are automatically generated. Each one performs the\n"
		@"\t; action of drawing only the subset of tiles marked as dirty according to the\n"
		@"\t; four bit code implied by its function number.\n"
		@"\t;\n"
		@"\t; i.e."
		@"\t;	* draw_left_sliver0 draws zero tiles because all dirty bits are clear;\n"
		@"\t;	* draw_left_sliver1 draws the first tile in its collection of four, but no others;\n"
		@"\t;	* draw_left_sliver9 draws the first and fourth tiles; and\n"
		@"\t;	* draw_left_sliver15 draws all four tiles.\n"
		@"\t; In all cases the first tile is the one lowest down the screen."
		@"\t;\n"
		@"\t; At exit:\n"
		@"\t;	* IX has been decremented by four; and\n"
		@"\t;	* HL points to the start address for the first tile above this group, if any.\n"
		@"\t;\n"
		@"\t; An initial sequence of JP statements provides for fast dispatch into the appropriate sliver.\n"
		@"\t;\n\n"
	];

	[code appendString:@"\tds align 256\n"];
	[code appendString:@"\tleft_slivers:\n"];
	for(int c = 0; c < 16; c++) {
		if(c) [code appendString:@"\t\tnop\n"];
		[code appendFormat:@"\t\tjp @+draw_left_sliver%d\n", c];
	}
	[code appendString:@"\n"];
	[code appendString:@"\tds align 256\n"];
	[code appendString:@"\tright_slivers:\n"];
	for(int c = 0; c < 16; c++) {
		if(c) [code appendString:@"\t\tnop\n"];
		[code appendFormat:@"\t\tjp @+draw_right_sliver%d\n", c];
	}
	[code appendString:@"\n"];

	for(NSString *side in @[@"left", @"right"]) {
		for(int c = 0; c < 16; c++) {
			// On input: IX points one beyond the next tile ID.
			// A contains the top byte of the tile dispatch table.
			// DE acts as the link register.

			[code appendFormat:@"\t@draw_%@_sliver%d:\n", side, c];
			[code appendString:@"\t\tld (@+return + 1), de\n"];

			int mask = 1;
			int offset = 0;
			auto append_offset = [&] {
				if(offset) {
					[code appendFormat:@"\t\tld bc, -%d\n", offset];
					[code appendString:@"\t\tadd hl, bc\n"];
				}
				offset = 0;
			};

			int load_slot = 0;
			int ix_offset = 1;
			while(mask < 16) {
				if(c & mask) {
					append_offset();
					offset = 15*128;

					const auto slot = load_slot++;
					[code appendFormat:@"\t\tld a, (ix - %d)\n", ix_offset];
					[code appendFormat:@"\t\tld (@+jpslot%d + 1), a\n", slot];
					[code appendString:@"\t\tld de, @+end_dispatch\n"];
					[code appendFormat:@"\t@jpslot%d:\n", slot];
					[code appendFormat:@"\t\tjp tiles_%@_7\n", side];
					[code appendString:@"\t@end_dispatch:\n"];
					[code appendString:@"\n"];
				} else {
					offset += 16*128;
				}

				mask <<= 1;
				++ix_offset;
			}
			append_offset();
			[code appendString:@"\t\tld bc, -4\n"];
			[code appendString:@"\t\tadd ix, bc\n"];
			[code appendString:@"\t@return:\n"];
			[code appendString:@"\t\tjp 1234\n\n"];
		}
	}

	[code
		writeToFile:[directory stringByAppendingPathComponent:@"slivers.z80s"]
		atomically:NO
		encoding:NSUTF8StringEncoding
		error:nil];
}

@end
