//
//  MandatoryRegisterAllocator.h
//  Map Preprocessor
//
//  Created by Thomas Harte on 09/11/2024.
//

/*!
	Attempts 'reasonably' to allocate registers to a timestamped stream of constants
	with the requirement that all constants must go into a register when they arrive.

	Side chat: this therefore models the Z80 scenario of writing out a stream of constants
	via SP, as the only way to write a value is by first putting it into a register.
*/
template <typename IntT>
class MandatoryRegisterAllocator {
public:
	MandatoryRegisterAllocator(size_t num_registers) : num_registers_(num_registers) {}
	
	void add_value(Time time, IntT value) {
		values_.emplace(time, value);
	}
	
private:
	size_t num_registers_;
	std::map<Time, IntT> values_;
};
