//
//  Register.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 18/11/2024.
//

#pragma once

namespace Register {

enum class Name {
	AF, BC, DE, HL, IX, IY,
	A, F, B, C, D, E, H, L,
	IXl, IXh, IYl, IYh,
	SP,
	SPl, SPh,
};

constexpr Name pair(Name r) {
	switch(r) {
		default: return r;

		case Name::B:
		case Name::C:	return Name::BC;
		case Name::D:
		case Name::E:	return Name::DE;
		case Name::H:
		case Name::L:	return Name::HL;
		case Name::A:
		case Name::F:	return Name::AF;

		case Name::IXl:
		case Name::IXh:	return Name::IX;
		case Name::IYl:
		case Name::IYh:	return Name::IY;
		case Name::SPl:
		case Name::SPh:	return Name::SP;
	}
}

constexpr const char *name(Name r) {
	switch(r) {
		case Name::AF:	return "af";
		case Name::BC:	return "bc";
		case Name::DE:	return "de";
		case Name::HL:	return "hl";
		case Name::IX:	return "ix";
		case Name::IY:	return "iy";
		case Name::SP:	return "sp";

		case Name::A:	return "a";
		case Name::F:	return "f";
		case Name::B:	return "b";
		case Name::C:	return "c";
		case Name::D:	return "d";
		case Name::E:	return "e";
		case Name::H:	return "h";
		case Name::L:	return "l";

		case Name::IXl:	return "ixl";
		case Name::IXh:	return "ixh";
		case Name::IYl: return "iyl";
		case Name::IYh:	return "iyh";
		case Name::SPl: return "spl";
		case Name::SPh:	return "sph";
	}
}

constexpr Name low_part(Name r) {
	switch(r) {
		case Name::AF:	return Name::F;
		case Name::BC:	return Name::C;
		case Name::DE:	return Name::E;
		case Name::HL:	return Name::L;
		case Name::IX:	return Name::IXl;
		case Name::IY:	return Name::IYl;
		case Name::SP:	return Name::SPl;

		case Name::A:	return Name::A;
		case Name::F:	return Name::F;
		case Name::B:	return Name::B;
		case Name::C:	return Name::C;
		case Name::D:	return Name::D;
		case Name::E:	return Name::E;
		case Name::H:	return Name::H;
		case Name::L:	return Name::L;

		case Name::IXl:	return Name::IXl;
		case Name::IXh:	return Name::IXh;
		case Name::IYl: return Name::IYl;
		case Name::IYh:	return Name::IYh;
		case Name::SPl: return Name::SPl;
		case Name::SPh:	return Name::SPh;
	}
}

constexpr Name high_part(Name r) {
	switch(r) {
		case Name::AF:	return Name::A;
		case Name::BC:	return Name::B;
		case Name::DE:	return Name::D;
		case Name::HL:	return Name::H;
		case Name::IX:	return Name::IXh;
		case Name::IY:	return Name::IYh;
		case Name::SP:	return Name::SPh;

		case Name::A:	return Name::A;
		case Name::F:	return Name::F;
		case Name::B:	return Name::B;
		case Name::C:	return Name::C;
		case Name::D:	return Name::D;
		case Name::E:	return Name::E;
		case Name::H:	return Name::H;
		case Name::L:	return Name::L;

		case Name::IXl:	return Name::IXl;
		case Name::IXh:	return Name::IXh;
		case Name::IYl: return Name::IYl;
		case Name::IYh:	return Name::IYh;
		case Name::SPl: return Name::SPl;
		case Name::SPh:	return Name::SPh;
	}
}

constexpr size_t size(Name r) {
	switch(r) {
		case Name::AF:
		case Name::BC:
		case Name::DE:
		case Name::HL:
		case Name::IX:
		case Name::IY:
		case Name::SP: return 2;

		case Name::A:
		case Name::F:
		case Name::B:
		case Name::C:
		case Name::D:
		case Name::E:
		case Name::H:
		case Name::L:
		case Name::IXl:
		case Name::IXh:
		case Name::IYl:
		case Name::IYh:
		case Name::SPl:
		case Name::SPh:	return 1;
	}
}

constexpr bool is_index_pair_or_hl(Name r) {
	switch(r) {
		default: return false;

		case Name::IX:
		case Name::IY:
		case Name::HL:
			return true;
	}
}

constexpr bool is_index_pair(Name r) {
	switch(r) {
		default: return false;

		case Name::IX:
		case Name::IY:
			return true;
	}
}

}
