//
//  RegisterAllocator.h
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 29/10/2024.
//

#pragma once

#include "TileSerialiser.h"

#include <unordered_map>
#include <optional>

struct RegisterEvent {
	enum class Type {
		Load, Reuse, UseConstant
	} type;
	enum class Register {
		BC, DE,
		B, C, D, E, A,
	} reg;
	uint16_t value;

	const char *push_register() const {
		switch(reg) {
			case RegisterEvent::Register::BC:	return "bc";
			case RegisterEvent::Register::DE:	return "de";
			case RegisterEvent::Register::B:	return "bc";
			case RegisterEvent::Register::C:	return "bc";
			case RegisterEvent::Register::D:	return "de";
			case RegisterEvent::Register::E:	return "de";
			case RegisterEvent::Register::A:	return "XXX";
		}
	}
	const char *load_register() const {
		switch(reg) {
			case RegisterEvent::Register::BC:	return "bc";
			case RegisterEvent::Register::DE:	return "de";
			case RegisterEvent::Register::B:	return "b";
			case RegisterEvent::Register::C:	return "c";
			case RegisterEvent::Register::D:	return "d";
			case RegisterEvent::Register::E:	return "e";
			case RegisterEvent::Register::A:	return "a";
		}
	}
};

template <int TileSize>
class RegisterAllocator {
	public:
		RegisterAllocator(TileSerialiser<TileSize> &serialiser) {
			// Count net frequencies of all words.
			while(true) {
				const auto event = serialiser.next();
				if(event.type == TileEvent::Type::Stop) {
					break;
				}

				switch(event.type) {
					default: break;
					case TileEvent::Type::OutputWord:
						++word_references_[event.content];
					break;
					case TileEvent::Type::OutputByte:
						++byte_references_[event.content];
					break;
				}
			}

			// Reset state.
			serialiser.reset();
		}

		RegisterEvent next_word(uint16_t value) {
			RegisterEvent event;

			// Simple, dumb strategy: reuse BC or DE if the value is already loaded.
			// Otherwise, if either register does not yet have a known value, use it.
			// Otherwise, evict whichever value has the least remaining usages.
			if(bc_ && *bc_ == value) {
				event.type = RegisterEvent::Type::Reuse;
				event.reg = RegisterEvent::Register::BC;
			} else if(de_ && *de_ == value) {
				event.type = RegisterEvent::Type::Reuse;
				event.reg = RegisterEvent::Register::DE;
			} else if(!bc_) {
				event.type = RegisterEvent::Type::Load;
				event.reg = RegisterEvent::Register::BC;
				event.value = value;
				bc_ = value;
			} else if(!de_) {
				event.type = RegisterEvent::Type::Load;
				event.reg = RegisterEvent::Register::DE;
				event.value = value;
				de_ = value;
			} else {
				event.type = RegisterEvent::Type::Load;
				if(word_references_[*bc_] < word_references_[*de_]) {
					if((*bc_ & 0xff00) == (value & 0xff00)) {
						event.reg = RegisterEvent::Register::C;
						event.value = value & 0xff;
					} else if((*bc_ & 0x00ff) == (value & 0x00ff)) {
						event.reg = RegisterEvent::Register::B;
						event.value = value >> 8;
					} else {
						event.reg = RegisterEvent::Register::BC;
						event.value = value;
					}
					bc_ = value;
				} else {
					if((*de_ & 0xff00) == (value & 0xff00)) {
						event.reg = RegisterEvent::Register::E;
						event.value = value & 0xff;
					} else if((*de_ & 0x00ff) == (value & 0x00ff)) {
						event.reg = RegisterEvent::Register::D;
						event.value = value >> 8;
					} else {
						event.reg = RegisterEvent::Register::DE;
						event.value = value;
					}
					de_ = value;
				}
			}

			// Reduce onward usage count.
			-- word_references_[value];
			return event;
		}

		RegisterEvent next_byte(uint8_t value) {
			if(bc_ && (*bc_ & 0xff) == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = RegisterEvent::Register::C};
			}
			if(bc_ && (*bc_ >> 8) == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = RegisterEvent::Register::B};
			}
			if(de_ && (*de_ & 0xff) == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = RegisterEvent::Register::E};
			}
			if(de_ && (*de_ >> 8) == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = RegisterEvent::Register::D};
			}

			// TODO: intelligent use of A?
			return RegisterEvent{.type = RegisterEvent::Type::UseConstant, .value = value};
		}

	private:
		std::unordered_map<uint16_t, int> word_references_;
		std::unordered_map<uint8_t, int> byte_references_;

		std::optional<uint16_t> bc_;
		std::optional<uint16_t> de_;
		std::optional<uint8_t> a_;
};
