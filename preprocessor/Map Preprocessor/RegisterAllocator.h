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
		BC, DE, HL, IX, IY,
		B, C, D, E, A, H, L,
	} reg;
	uint16_t value;
	std::optional<uint16_t> previous_value;

	const char *push_register() const {
		switch(reg) {
			case RegisterEvent::Register::BC:	return "bc";
			case RegisterEvent::Register::DE:	return "de";
			case RegisterEvent::Register::HL:	return "bc";
			case RegisterEvent::Register::IX:	return "ix";
			case RegisterEvent::Register::IY:	return "iy";

			case RegisterEvent::Register::B:	return "bc";
			case RegisterEvent::Register::C:	return "bc";
			case RegisterEvent::Register::D:	return "de";
			case RegisterEvent::Register::E:	return "de";
			case RegisterEvent::Register::H:	return "hl";
			case RegisterEvent::Register::L:	return "hl";
			case RegisterEvent::Register::A:	return "af";
		}
	}
	const char *load_register() const {
		switch(reg) {
			case RegisterEvent::Register::BC:	return "bc";
			case RegisterEvent::Register::DE:	return "de";
			case RegisterEvent::Register::HL:	return "bc";
			case RegisterEvent::Register::IX:	return "ix";
			case RegisterEvent::Register::IY:	return "iy";

			case RegisterEvent::Register::B:	return "b";
			case RegisterEvent::Register::C:	return "c";
			case RegisterEvent::Register::D:	return "d";
			case RegisterEvent::Register::E:	return "e";
			case RegisterEvent::Register::H:	return "h";
			case RegisterEvent::Register::L:	return "l";
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
				}
			}

			// Look for IY and A optimisations.
			serialiser.reset();

			// Reset state.
			reset();
			auto word_references = word_references_;
			while(true) {
				const auto next = serialiser.next();
				if(next.type == TileEvent::Type::Stop) {
					break;
				}

				switch(next.type) {
					default: break;

					case TileEvent::Type::OutputWord: {
						// const auto event =
						next_word(next.content);
					} break;

					case TileEvent::Type::OutputByte: {
						const auto event = next_byte(next.content);
						if(event.type == RegisterEvent::Type::UseConstant) {
							++byte_references_[event.value];
						}
					} break;
				}
			}

			// Restore original word references list.
			word_references_ = word_references;

			// A allocation strategy is as dense as can be: use the most-recurring
			// value, as long as it's at least 3.
			if(!byte_references_.empty()) {
				auto greatest = byte_references_.begin();
				auto current = greatest;
				++current;
				while(current != byte_references_.end()) {
					if(current->second > greatest->second) {
						greatest = current;
					}
					++current;
				}
				if(greatest->second > 2) {
					a_ = greatest->first;
				}
			}

			// Clear state.
			is_preview_ = false;
			serialiser.reset();
			reset();
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
				event.previous_value = bc_;
				bc_ = value;
			} else if(!de_) {
				event.type = RegisterEvent::Type::Load;
				event.reg = RegisterEvent::Register::DE;
				event.value = value;
				event.previous_value = de_;
				de_ = value;
			} else {
				event.type = RegisterEvent::Type::Load;
				if(word_references_[*bc_] < word_references_[*de_]) {
					event.reg = RegisterEvent::Register::BC;
					event.value = value;
					event.previous_value = bc_;
					bc_ = value;
				} else {
					event.reg = RegisterEvent::Register::DE;
					event.value = value;
					event.previous_value = de_;
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

			if(a_ && *a_ == value) {
				bool is_load = !has_loaded_a_;
				has_loaded_a_ = true;
				return RegisterEvent{.reg = RegisterEvent::Register::A, .type = is_load ? RegisterEvent::Type::Load : RegisterEvent::Type::Reuse, .value = value};
			} else {
				return RegisterEvent{.type = RegisterEvent::Type::UseConstant, .value = value};
			}
		}

		void reset() {
			bc_ = {};
			de_ = {};
		}

	private:
		std::unordered_map<uint16_t, int> word_references_;
		std::unordered_map<uint8_t, int> byte_references_;

		std::optional<uint16_t> bc_;
		std::optional<uint16_t> de_;
		std::optional<uint8_t> a_;

		bool is_preview_ = true;
		bool has_loaded_a_ = false;
};
