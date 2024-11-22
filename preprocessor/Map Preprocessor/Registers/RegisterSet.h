//
//  RegisterSet.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 19/11/2024.
//

#pragma once

#include "Register.h"
#include "Operation.h"

/// Models the full set of Z80 registers of which this program's code generators are aware
/// and provides the minimal route to loading values to registers given their current state.
///
/// E.g. if BC is currently 0x23 and the request is to load BC with 0x23, no operation is generated.
/// If it's to load BC with 0x123 then a load to B is generated. If it's to load BC with 0x24 then an
/// INC C is generated. Etc.
class RegisterSet {
public:
	template <typename IntT>
	Operation load(Register::Name reg, IntT target) {
		const auto previous = value<IntT>(reg);
		struct SetAtExit {
			SetAtExit(Register::Name reg, IntT target, RegisterSet &set) :
				reg_(reg), target_(target), set_(set) {}

			~SetAtExit() {
				set_.set_value<IntT>(reg_, target_);
			}

			Register::Name reg_;
			IntT target_;
			RegisterSet &set_;
		};
		SetAtExit at_exit(reg, target, *this);

		if(previous && *previous == target) {
			return Operation::nullary(Operation::Type::NONE);
		}

		// If this is a pair in which only one half has changed, do only a byte operation.
		if(Register::size(reg) == 2) {
			if(value<uint8_t>(Register::high_part(reg)) == (target >> 8)) {
				return load<uint8_t>(Register::low_part(reg), target & 0xff);
			}

			if(value<uint8_t>(Register::low_part(reg)) == (target & 0xff)) {
				return load<uint8_t>(Register::high_part(reg), target >> 8);
			}
		}

		if(previous) {
			if(target == IntT(*previous + 1)) {
				return Operation::unary(Operation::Type::INC, reg);
			}

			if(target == IntT(*previous - 1)) {
				return Operation::unary(Operation::Type::DEC, reg);
			}

			if(reg == Register::Name::A) {
				if(target == std::rotr(*previous, 1)) {
					return Operation::nullary(Operation::Type::RRCA);
				}

				if(target == std::rotl(*previous, 1)) {
					return Operation::nullary(Operation::Type::RLCA);
				}

				if(target == (*previous^0xff)) {
					return Operation::nullary(Operation::Type::CPL);
				}
			}
		}

		// If this is A, also consider simple manipulations.
		if(reg == Register::Name::A) {
			if(!target) {
				return Operation::unary(Operation::Type::XOR, Register::Name::A);
			}

			if(previous) {
				for(const auto source: {
					Register::Name::B, Register::Name::C, Register::Name::D,
					Register::Name::E, Register::Name::H, Register::Name::L,
				}) {
					const auto source_value = value<uint8_t>(source);
					if(!source_value) continue;

					if(uint8_t(*previous + *source_value) == target) {
						return Operation::unary(Operation::Type::ADD, source);
					}

					if(uint8_t(*previous - *source_value) == target) {
						return Operation::unary(Operation::Type::SUB, source);
					}

					if(uint8_t(*previous | *source_value) == target) {
						return Operation::unary(Operation::Type::OR, source);
					}

					if(uint8_t(*previous ^ *source_value) == target) {
						return Operation::unary(Operation::Type::XOR, source);
					}

					if(uint8_t(*previous & *source_value) == target) {
						return Operation::unary(Operation::Type::AND, source);
					}
				}
			}
		}

		// If this isn't a pair but the value already exists elsewhere in the register
		// set, just load it from there (unless it's _from_ an index register because that
		// doesn't save anything, or involves two index registers versus HL).
		if(Register::size(reg) == 1) {
			const auto source = find<uint8_t>(target);
			if(
				source &&
				!Register::is_index_pair(Register::pair(*source)) &&
				!(Register::is_index_pair(Register::pair(reg)) && Register::pair(*source) == Register::Name::HL)
			) {
				return Operation::ld(reg, *source);
			}
		}

		if(Register::size(reg) == 2) {
			return Operation::ld(Operand::direct(reg), Operand::immediate<uint16_t>(target));
		} else {
			return Operation::ld(Operand::direct(reg), Operand::immediate<uint8_t>(target));
		}
	}

	template <typename IntT>
	std::optional<IntT> value(Register::Name r) const {
		switch(r) {
			case Register::Name::A:		if(a_) { return *a_; } 		break;
			case Register::Name::F:		if(f_) { return *f_; } 		break;
			case Register::Name::B:		if(b_) { return *b_; }		break;
			case Register::Name::C:		if(c_) { return *c_; }		break;
			case Register::Name::D:		if(d_) { return *d_; }		break;
			case Register::Name::E:		if(e_) { return *e_; }		break;
			case Register::Name::H:		if(h_) { return *h_; }		break;
			case Register::Name::L:		if(l_) { return *l_; }		break;
			case Register::Name::IXh:	if(ixh_) { return *ixh_; }	break;
			case Register::Name::IXl:	if(ixl_) { return *ixl_; }	break;
			case Register::Name::IYh:	if(iyh_) { return *iyh_; }	break;
			case Register::Name::IYl:	if(iyl_) { return *iyl_; }	break;
			case Register::Name::SPh:	if(sph_) { return *sph_; }	break;
			case Register::Name::SPl:	if(spl_) { return *spl_; }	break;

			case Register::Name::AF:	if(a_ && f_) 		{ return *f_ | *a_ << 8; }			break;
			case Register::Name::BC:	if(b_ && c_) 		{ return *c_ | *b_ << 8; }			break;
			case Register::Name::DE:	if(d_ && e_) 		{ return *e_ | *d_ << 8; }			break;
			case Register::Name::HL:	if(h_ && l_) 		{ return *l_ | *h_ << 8; }			break;
			case Register::Name::IX:	if(ixh_ && ixl_)	{ return *ixl_ | *ixh_ << 8; }		break;
			case Register::Name::IY:	if(iyh_ && iyl_)	{ return *iyl_ | *iyh_ << 8; }		break;
			case Register::Name::SP:	if(sph_ && spl_)	{ return *spl_ | *sph_ << 8; }		break;
		}

		return {};
	}

	template <typename IntT>
	std::optional<Register::Name> find(IntT key) const {
		if constexpr (std::is_same_v<IntT, uint16_t>) {
			const auto is_equal = [&](Register::Name pair) -> bool {
				const auto pair_value = value<uint16_t>(pair);
				if(!pair_value) return false;
				return *pair_value == key;
			};

			for(auto &source: {
					Register::Name::BC,
					Register::Name::DE,
					Register::Name::HL,
					Register::Name::IX,
					Register::Name::IY}
				) {
				if(is_equal(source)) return source;
			}
		}

		if constexpr (std::is_same_v<IntT, uint8_t>) {
			const auto is_equal = [&](Register::Name r) -> bool {
				const auto r_value = value<uint8_t>(r);
				if(!r_value) return false;
				return *r_value == key;
			};

			for(auto &source: {
					Register::Name::A,
					Register::Name::B, 		Register::Name::C,
					Register::Name::D, 		Register::Name::E,
					Register::Name::H, 		Register::Name::L,
					Register::Name::IXh,	Register::Name::IXl,
					Register::Name::IYh,	Register::Name::IYl}
				) {
				if(is_equal(source)) return source;
			}
		}

		return {};
	}

	template <typename IntT>
	void set_value(Register::Name r, IntT value) {
		switch(r) {
			case Register::Name::A:		a_ = value;		break;
			case Register::Name::F:		f_ = value;		break;
			case Register::Name::B:		b_ = value;		break;
			case Register::Name::C:		c_ = value;		break;
			case Register::Name::D:		d_ = value;		break;
			case Register::Name::E:		e_ = value;		break;
			case Register::Name::H:		h_ = value;		break;
			case Register::Name::L:		l_ = value;		break;
			case Register::Name::IXh:	ixh_ = value;	break;
			case Register::Name::IXl:	ixl_ = value;	break;
			case Register::Name::IYh:	iyh_ = value;	break;
			case Register::Name::IYl:	iyl_ = value;	break;
			case Register::Name::SPh:	sph_ = value;	break;
			case Register::Name::SPl:	spl_ = value;	break;

			case Register::Name::AF:	a_ = uint8_t(value >> 8); f_ = uint8_t(value);		break;
			case Register::Name::BC:	b_ = uint8_t(value >> 8); c_ = uint8_t(value);		break;
			case Register::Name::DE:	d_ = uint8_t(value >> 8); e_ = uint8_t(value);		break;
			case Register::Name::HL:	h_ = uint8_t(value >> 8); l_ = uint8_t(value);		break;
			case Register::Name::IX:	ixh_ = uint8_t(value >> 8); ixl_ = uint8_t(value);	break;
			case Register::Name::IY:	iyh_ = uint8_t(value >> 8); iyl_ = uint8_t(value);	break;
			case Register::Name::SP:	sph_ = uint8_t(value >> 8); spl_ = uint8_t(value);	break;
		}
	}

private:
	std::optional<uint8_t> a_;
	std::optional<uint8_t> f_;
	std::optional<uint8_t> b_;
	std::optional<uint8_t> c_;
	std::optional<uint8_t> d_;
	std::optional<uint8_t> e_;
	std::optional<uint8_t> h_;
	std::optional<uint8_t> l_;
	std::optional<uint8_t> ixh_;
	std::optional<uint8_t> ixl_;
	std::optional<uint8_t> iyh_;
	std::optional<uint8_t> iyl_;
	std::optional<uint8_t> sph_;
	std::optional<uint8_t> spl_;
};
