//
//  Register.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 18/11/2024.
//

#pragma once

namespace Register {

enum class Name {
	BC, DE, HL, IX, IY,
	B, C, D, E, A, H, L,
	IXl, IXh, IYl, IYh,
};

constexpr const char *pair_name(Name r) {
	switch(r) {
		case Name::BC:	return "bc";
		case Name::DE:	return "de";
		case Name::HL:	return "bc";
		case Name::IX:	return "ix";
		case Name::IY:	return "iy";
			
		case Name::B:
		case Name::C:	return "bc";
		case Name::D:
		case Name::E:	return "de";
		case Name::H:
		case Name::L:	return "hl";
		case Name::A:	return "af";
			
		case Name::IXl:
		case Name::IXh:	return "ix";
		case Name::IYl:
		case Name::IYh:	return "iy";
	}
}

constexpr const char *name(Name r) {
	switch(r) {
		case Name::BC:	return "bc";
		case Name::DE:	return "de";
		case Name::HL:	return "bc";
		case Name::IX:	return "ix";
		case Name::IY:	return "iy";
			
		case Name::B:	return "b";
		case Name::C:	return "c";
		case Name::D:	return "d";
		case Name::E:	return "e";
		case Name::H:	return "h";
		case Name::L:	return "l";
		case Name::A:	return "a";
			
		case Name::IXl:	return "ixl";
		case Name::IXh:	return "ixh";
		case Name::IYl: return "iyl";
		case Name::IYh:	return "iyh";
	}
}

constexpr Name low_part(Name r) {
	switch(r) {
		case Name::BC:	return Name::C;
		case Name::DE:	return Name::E;
		case Name::HL:	return Name::L;
		case Name::IX:	return Name::IXl;
		case Name::IY:	return Name::IYl;
			
		case Name::B:	return Name::B;
		case Name::C:	return Name::C;
		case Name::D:	return Name::D;
		case Name::E:	return Name::E;
		case Name::H:	return Name::H;
		case Name::L:	return Name::L;
		case Name::A:	return Name::A;
			
		case Name::IXl:	return Name::IXl;
		case Name::IXh:	return Name::IXh;
		case Name::IYl: return Name::IYl;
		case Name::IYh:	return Name::IYh;
	}
}

constexpr Name high_part(Name r) {
	switch(r) {
		case Name::BC:	return Name::B;
		case Name::DE:	return Name::D;
		case Name::HL:	return Name::H;
		case Name::IX:	return Name::IXh;
		case Name::IY:	return Name::IYh;
			
		case Name::B:	return Name::B;
		case Name::C:	return Name::C;
		case Name::D:	return Name::D;
		case Name::E:	return Name::E;
		case Name::H:	return Name::H;
		case Name::L:	return Name::L;
		case Name::A:	return Name::A;
			
		case Name::IXl:	return Name::IXl;
		case Name::IXh:	return Name::IXh;
		case Name::IYl: return Name::IYl;
		case Name::IYh:	return Name::IYh;
	}
}

constexpr size_t size(Name r) {
	switch(r) {
		case Name::BC:
		case Name::DE:
		case Name::HL:
		case Name::IX:
		case Name::IY:	return 2;
			
		case Name::B:
		case Name::C:
		case Name::D:
		case Name::E:
		case Name::H:
		case Name::L:
		case Name::A:
			
		case Name::IXl:
		case Name::IXh:
		case Name::IYl:
		case Name::IYh:	return 1;
	}
}

}
