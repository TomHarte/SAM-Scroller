//
//  Operation.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 19/11/2024.
//

#pragma once

#include "Register.h"
#include <optional>
#include <variant>

struct Operand {
	enum class Type {
		Direct,
		Indirect,
		Immediate,
		Label,
		LabelIndirect,
	} type;
	std::variant<Register::Name, uint16_t, uint8_t, std::string> value;

	static Operand label(const char *name) {
		return Operand{
			.type = Type::Label,
			.value = std::string(name)
		};
	}
	static Operand label_indirect(const char *name) {
		return Operand{
			.type = Type::LabelIndirect,
			.value = std::string(name)
		};
	}
	static Operand direct(Register::Name name) {
		return Operand{
			.type = Type::Direct,
			.value = name
		};
	}
	static Operand indirect(Register::Name name) {
		return Operand{
			.type = Type::Indirect,
			.value = name
		};
	}
	template <typename IntT>
	static Operand immediate(IntT v) {
		return Operand{
			.type = Type::Immediate,
			.value = v
		};
	}

	bool is_index_pair() const {
		if(const auto* reg = std::get_if<Register::Name>(&value)) {
			return Register::is_index_pair(*reg);
		}
		return false;
	}

	size_t index_cost() const {
		if(const auto* reg = std::get_if<Register::Name>(&value)) {
			return Register::is_index_pair(*reg) ? 1 : 0;
		}
		return 0;
	}

	size_t size() const {
		if(const auto* reg = std::get_if<Register::Name>(&value)) {
			return Register::size(*reg);
		}
		return 0;
	}

	NSString *text() const {
		switch(type) {
			case Type::Direct:			return [NSString stringWithFormat:@"%s", Register::name(std::get<Register::Name>(value))];
			case Type::Indirect:		return [NSString stringWithFormat:@"(%s)", Register::name(std::get<Register::Name>(value))];
			case Type::Label:			return [NSString stringWithFormat:@"%s", std::get<std::string>(value).c_str()];
			case Type::LabelIndirect:	return [NSString stringWithFormat:@"(%s)", std::get<std::string>(value).c_str()];
			case Type::Immediate:
				if(const uint8_t *value8 = std::get_if<uint8_t>(&value)) {
					return [NSString stringWithFormat:@"0x%02x", *value8];
				}
				return [NSString stringWithFormat:@"0x%04x", std::get<uint16_t>(value)];
		}
	}
};

struct Operation {
	enum class Type {
		LD,
		INC, DEC,
		RRCA, RLCA, CPL,
		ADD, SUB, OR, XOR, AND,
		PUSH,
		JP,

		BLANK_LINE,
		NONE,
		LABEL,
	} type;
	std::optional<Operand> destination;
	std::optional<Operand> source;

	static Operation nullary(Type type) {	return Operation{.type = type};	}
	static Operation unary(Type type, Register::Name destination) {
		return Operation{
			.type = type,
			.destination = Operand::direct(destination),
		};
	}

	static Operation label(const char *name) {
		return Operation{
			.type = Type::LABEL,
			.destination = Operand::label(name),
		};
	}
	static Operation ld(Operand destination, Operand source) {
		return Operation{
			.type = Type::LD,
			.destination = destination,
			.source = source,
		};
	}
	static Operation ld(Register::Name destination, Register::Name source) {
		return ld(Operand::direct(destination), Operand::direct(source));
	}
	static Operation add(Register::Name destination, Register::Name source) {
		return Operation{
			.type = Type::ADD,
			.destination = Operand::direct(destination),
			.source = Operand::direct(source),
		};
	}
	static Operation jp(uint16_t destination) {
		return Operation{
			.type = Type::JP,
			.destination = Operand::immediate(destination),
		};
	}

	NSString *text() const {
		NSMutableString *text = [[NSMutableString alloc] init];
		switch(type) {
			case Type::LD:		[text appendString:@"ld"];		break;
			case Type::INC:		[text appendString:@"inc"];		break;
			case Type::DEC:		[text appendString:@"dec"];		break;
			case Type::ADD:		[text appendString:@"add"];		break;
			case Type::SUB:		[text appendString:@"sub"];		break;
			case Type::OR:		[text appendString:@"or"];		break;
			case Type::XOR:		[text appendString:@"xor"];		break;
			case Type::AND:		[text appendString:@"and"];		break;
			case Type::PUSH:	[text appendString:@"push"];	break;
			case Type::JP:		[text appendString:@"jp"];		break;

			case Type::NONE:
			case Type::BLANK_LINE:	return @"";
			case Type::RRCA:	return @"rrca";		break;
			case Type::RLCA:	return @"rlca";		break;
			case Type::CPL:		return @"cpl";		break;

			case Type::LABEL:	return [NSString stringWithFormat:@"%@:", destination->text()];
		}

		if(destination) {
			[text appendFormat:@" %@", destination->text()];
		}
		if(source) {
			[text appendFormat:@", %@", source->text()];
		}
		return text;
	}

	/// Provides a SAM-centric costing of the included operation;
	/// usually this will be the number of memory windows covered,
	/// during the 75% of the display that requires only 4-cycle alignment.
	size_t cost() const {
		switch(type) {
			case Type::LD: {

				// Register to register loads must be 8-bit.
				if(
					destination->type == Operand::Type::Direct &&
					source->type == Operand::Type::Direct
				) {
					if(source->is_index_pair() || destination->is_index_pair()) {
						return 2;
					}
					return 1;
				}

				// There's really only LD (HL), n in this category.
				if(
					destination->type == Operand::Type::Indirect &&
					source->type == Operand::Type::Immediate
				) {
					return 3 + destination->index_cost();
				}

				// Hopefully that leaves only LD r,n and LR r,nn, at least
				// as far as this project is concerned.
				if(
					destination->type == Operand::Type::Direct &&
					source->type == Operand::Type::Immediate
				) {
					return destination->index_cost() + destination->size() + 1;
				}
			} break;

			case Type::INC:
			case Type::DEC:
				return destination->index_cost() + destination->size();
			break;

			case Type::PUSH:	return 3 + destination->index_cost();
			case Type::ADD:		return destination->size();

			case Type::SUB:
			case Type::XOR:
			case Type::OR:
			case Type::AND:
			case Type::RLCA:
			case Type::RRCA:
			case Type::CPL:		return 1;

			case Type::JP:		return 3;

			case Type::LABEL:
			case Type::BLANK_LINE:
			case Type::NONE:	return 0;
		}

		assert(false);
		return 99;
	}
};