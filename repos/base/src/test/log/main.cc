/*
 * \brief  Testing 'log()' with negative integer
 * \author Christian Prochaska
 * \date   2012-04-20
 *
 */

/*
 * Copyright (C) 2012-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/component.h>
#include <base/log.h>


void Component::construct(Genode::Env &env)
{
	using namespace Genode;

	log("hex range:          ", Hex_range<uint16_t>(0xe00, 0x880));
	log("empty hex range:    ", Hex_range<uint32_t>(0xabc0000, 0));
	log("hex range to limit: ", Hex_range<uint8_t>(0xf8, 8));
	log("invalid hex range:  ", Hex_range<uint8_t>(0xf8, 0x10));
	log("negative hex char:  ", Hex((char)-2LL, Hex::PREFIX, Hex::PAD));
	log("positive hex char:  ", Hex((char) 2LL, Hex::PREFIX, Hex::PAD));

	typedef String<128> Label;
	log("multiarg string:    ", Label(Char('"'), "parent -> child.", 7, Char('"')));

	String<32> hex(Hex(3));
	log("String(Hex(3)):     ", hex);

	log("sizeof size_t: ", sizeof(size_t));
	log("sizeof addr_t: ", sizeof(addr_t));
	log("sizeof off_t: ", sizeof(off_t));
	log("sizeof long: ", sizeof(long));

	log("Test done.");
	env.parent().exit(0);
}
