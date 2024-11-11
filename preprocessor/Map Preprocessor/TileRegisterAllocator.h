//
//  RegisterAllocator.h
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 29/10/2024.
//

#pragma once

#include "OptionalRegisterAllocator.h"
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
class TileRegisterAllocator {
public:
	TileRegisterAllocator(TileSerialiser<TileSize> &serialiser) : a_cursor_(a_allocations_.end()) {
		// Accumulate word priorities.
		Time time = 0;
		while(true) {
			++time;
			const auto event = serialiser.next();
			if(event.type == TileEvent::Type::Stop) {
				break;
			}

			switch(event.type) {
				default: break;
				case TileEvent::Type::OutputWord:
					word_priorities_.add_value(time, event.content);
				break;
			}
		}

		// Look for IY and A optimisations.
		serialiser.reset();

		// Reset state.
		reset();
		OptionalRegisterAllocator<uint8_t> a_allocator(1);
		OptionalRegisterAllocator<uint16_t> iy_allocator(1);
		time = 0;
		while(true) {
			++time;
			const auto next = serialiser.next();
			if(next.type == TileEvent::Type::Stop) {
				break;
			}

			switch(next.type) {
				default: break;

				case TileEvent::Type::OutputWord: {
					// By catching only loads, look for values that — even after other optimisation — are
					// nevertheless repeated in the stream. Consider using IY for those.
					const auto event = next_word(next.content);
					if(event.type == RegisterEvent::Type::Load) {
						iy_allocator.add_value(time, event.value);
					}
				} break;

				case TileEvent::Type::OutputByte: {
					const auto event = next_byte(next.content);
					if(event.type == RegisterEvent::Type::UseConstant) {
						a_allocator.add_value(time, event.value);
					}
				} break;
			}
		}

		// Restore original word references list and grab the allocation list for A.
		a_allocations_ = a_allocator.spans();
		iy_allocations_ = iy_allocator.spans();

		// Remove IY allocations from the main allocator.
		time = 0;
		std::optional<uint16_t> iy_;
		for(const auto &allocation: iy_allocations_) {
			if(iy_) {
				word_priorities_.remove_value(time, allocation.time, *iy_);
			}
			iy_ = allocation.value;
			time = allocation.time;
		}
		if(iy_) {
			word_priorities_.remove_value(time, word_priorities_.end_time(), *iy_);
		}

		// Clear state.
		serialiser.reset();
		reset();
	}

	RegisterEvent next_word(uint16_t value) {
		++time_;
		RegisterEvent event;
		
		if(iy_cursor_ != iy_allocations_.end() && time_ == iy_cursor_->time) {
			auto previous = iy_;
			iy_ = value;
			return RegisterEvent{.reg = RegisterEvent::Register::IY, .type = RegisterEvent::Type::Load, .previous_value = previous, .value = value};
		}
		
		if(iy_ && value == *iy_) {
			return RegisterEvent{.reg = RegisterEvent::Register::IY, .type = RegisterEvent::Type::Reuse};
		}

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
			if(
				word_priorities_.priority_at(time_, word_priorities_.end_time(), *bc_) <
				word_priorities_.priority_at(time_, word_priorities_.end_time(), *de_)
			) {
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
		return event;
	}

	RegisterEvent next_byte(uint8_t value) {
		++time_;
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
		if(a_cursor_ != a_allocations_.end() && time_ == a_cursor_->time) {
			auto previous = a_;
			a_ = value;
			return RegisterEvent{.reg = RegisterEvent::Register::A, .type = RegisterEvent::Type::Load, .previous_value = previous, .value = value};
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
		iy_ = {};
		time_ = 0;
		a_cursor_ = a_allocations_.begin();
		iy_cursor_ = iy_allocations_.begin();
	}

private:
	Prioritiser<uint16_t> word_priorities_;

	std::optional<uint16_t> bc_;
	std::optional<uint16_t> de_;
	std::optional<uint16_t> iy_;
	std::optional<uint8_t> a_;

	std::vector<Allocation<uint8_t>> a_allocations_;
	std::vector<Allocation<uint16_t>> iy_allocations_;
	std::vector<Allocation<uint8_t>>::iterator a_cursor_;
	std::vector<Allocation<uint16_t>>::iterator iy_cursor_;
	Time time_;
};