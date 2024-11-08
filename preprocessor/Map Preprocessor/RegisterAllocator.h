//
//  RegisterAllocator.h
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 29/10/2024.
//

#pragma once

#include "RangeAllocator.h"
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
	RegisterAllocator(TileSerialiser<TileSize> &serialiser) : a_cursor_(a_allocations_.end()) {
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
			RangeAllocator<uint8_t> a_allocator(1);
			Time time = 0;
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
							++time;
							a_allocator.add_value(time, event.value);
						}
					} break;
				}
			}

			// Restore original word references list and grab the allocation list for A.
			word_references_ = word_references;
			a_allocations_ = a_allocator.spans();

			// Clear state.
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

			// Is this a point at which A is loaded?
			++a_time_;
			if(a_cursor_ != a_allocations_.end() && a_time_ == a_cursor_->time) {
				a_ = value;
				return RegisterEvent{.reg = RegisterEvent::Register::A, .type = RegisterEvent::Type::Load, .value = value};
			}

			// Otherwise, does A have the right value already?
			if(a_ && *a_ == value) {
				return RegisterEvent{.reg = RegisterEvent::Register::A, .type = RegisterEvent::Type::Reuse, .value = value};
			} else {
				return RegisterEvent{.type = RegisterEvent::Type::UseConstant, .value = value};
			}
		}

		void reset() {
			bc_ = {};
			de_ = {};
			a_ = {};
			a_time_ = 0;
			a_cursor_ = a_allocations_.begin();
		}

	private:
		std::unordered_map<uint16_t, int> word_references_;

		std::optional<uint16_t> bc_;
		std::optional<uint16_t> de_;
		std::optional<uint8_t> a_;
		std::vector<Allocation<uint8_t>> a_allocations_;
		std::vector<Allocation<uint8_t>>::iterator a_cursor_;
		Time a_time_;
};
