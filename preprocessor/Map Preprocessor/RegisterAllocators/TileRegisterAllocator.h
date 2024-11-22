//
//  RegisterAllocator.h
//  Gryzor Preprocessor
//
//  Created by Thomas Harte on 29/10/2024.
//

#pragma once

#include "OptionalRegisterAllocator.h"
#include "MandatoryRegisterAllocator.h"
#include "TileSerialiser.h"
#include "Register.h"

#include <unordered_map>
#include <optional>

struct RegisterEvent {
	enum class Type {
		Load, Reuse, UseConstant
	} type;
	Register::Name reg;
	uint16_t value;
};

template <int TileSize>
class TileRegisterAllocator {
	static constexpr auto RegistersSansIX = { Register::Name::BC, Register::Name::DE, Register::Name::IY };
	static constexpr auto RegistersPlusIX = { Register::Name::BC, Register::Name::DE, Register::Name::IY, Register::Name::IX };

public:
	TileRegisterAllocator(TileSerialiser<TileSize> &serialiser, bool permit_ix) :
		a_cursor_(a_allocations_.end()),
		registers_(permit_ix ? RegistersPlusIX : RegistersSansIX)
	{
		MandatoryRegisterAllocator<uint16_t> allocator(registers_);
		serialiser.reset();

		// Accumulate word priorities.
		while(true) {
			const auto event = serialiser.next();
			if(event.type == TileEvent::Type::Stop) {
				break;
			}

			switch(event.type) {
				default: break;
				case TileEvent::Type::OutputWord:
					allocator.add_value(serialiser.event_offset(), event.content);
				break;
			}
		}

		allocations_ = allocator.spans();

		// Reset state.
		serialiser.reset();
		reset();

		// Look for A optimisations.
		const auto registers8 = { Register::Name::A };
		OptionalRegisterAllocator<uint8_t> a_allocator(registers8);
		while(true) {
			const auto next = serialiser.next();
			if(next.type == TileEvent::Type::Stop) {
				break;
			}

			switch(next.type) {
				default: break;

				case TileEvent::Type::OutputByte: {
					const auto event = next_byte(serialiser.event_offset(), next.content);
					if(event.type == RegisterEvent::Type::UseConstant) {
						a_allocator.add_value(serialiser.event_offset(), event.value);
					}
				} break;
			}
		}
		a_allocations_ = a_allocator.spans();

		// Clear state.
		serialiser.reset();
		reset();
	}

	RegisterEvent next_word(size_t time, uint16_t value) {
		 if(cursor_ != allocations_.end() && time == cursor_->time) {
			 state_.set_value<uint16_t>(cursor_->reg, cursor_->value);
			 const auto cursor = cursor_;
			 ++cursor_;

			 return RegisterEvent {
				.reg = cursor->reg,
				.type = RegisterEvent::Type::Load,
				.value = cursor->value,
			 };
		 }

		// Now it should be true that `value` is definitely, definitely in
		// the register set.
		for(auto reg: registers_) {
			const auto reg_value = state_.value<uint16_t>(reg);
			if(reg_value && *reg_value == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = reg};
			}
		}

		// Should be impossible.
		assert(false);
		return RegisterEvent{};
	}

	RegisterEvent next_byte(size_t time, uint8_t value) {
		// Test for existence in B, C, D, E.
		for(auto reg: registers_) {
			if(Register::is_index_pair(reg)) {
				continue;
			}

			const auto high = state_.value<uint8_t>(Register::high_part(reg));
			const auto low = state_.value<uint8_t>(Register::low_part(reg));

			if(high && *high == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = Register::high_part(reg)};
			}
			if(low && *low == value) {
				return RegisterEvent{.type = RegisterEvent::Type::Reuse, .reg = Register::low_part(reg)};
			}
		}

		// Is this a point at which A is loaded?
		if(a_cursor_ != a_allocations_.end() && time == a_cursor_->time) {
			state_.set_value<uint8_t>(Register::Name::A, value);
			++a_cursor_;
			return RegisterEvent{.reg = Register::Name::A, .type = RegisterEvent::Type::Load, .value = value};
		}

		// Otherwise, does A have the right value already or is this a constant?
		const auto a = state_.value<uint8_t>(Register::Name::A);
		if(a && *a == value) {
			return RegisterEvent{.reg = Register::Name::A, .type = RegisterEvent::Type::Reuse, .value = value};
		} else {
			return RegisterEvent{.type = RegisterEvent::Type::UseConstant, .value = value};
		}
	}

	void reset() {
		state_ = RegisterSet{};
		a_cursor_ = a_allocations_.begin();
		cursor_ = allocations_.begin();
	}

private:
	std::vector<Register::Name> registers_;
	RegisterSet state_;

	std::vector<Allocation<uint16_t>> allocations_;
	std::vector<Allocation<uint16_t>>::iterator cursor_;

	std::vector<Allocation<uint8_t>> a_allocations_;
	std::vector<Allocation<uint8_t>>::iterator a_cursor_;
};
