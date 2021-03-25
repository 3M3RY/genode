/*
 * \brief  I2C session component
 * \author Jean-Adrien Domage <jean-adrien.domage@gapfruit.com>
 * \date   2021-02-26
 */

/*
 * Copyright (C) 2013-2021 Genode Labs GmbH
 * Copyright (C) 2021 gapfruit AG
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _I2C_DRIVER__COMPONENT_H_
#define _I2C_DRIVER__COMPONENT_H_

#include <root/component.h>
#include <i2c_session/i2c_session.h>
#include "i2c_interface.h"

namespace I2c {
	using namespace Genode;
	class Session_component;
	class Root;
}


class I2c::Session_component : public Rpc_object<I2c::Session>
{
	private:

		Rpc_entrypoint   &_ep;
		I2c::Driver_base &_driver;
		uint8_t const     _device_address;

	public:

		Session_component(Rpc_entrypoint   &ep,
		                  I2c::Driver_base &driver,
		                  uint8_t           device_address)
		:
			_ep(ep), _driver(driver), _device_address(device_address)
		{ }

		void write_8bits(uint8_t byte) override
		{
			_driver.write(_device_address, &byte, sizeof(byte));
		}

		uint8_t read_8bits() override
		{
			uint8_t data = 0;
			_driver.read(_device_address, reinterpret_cast<uint8_t*>(&data), sizeof(data));
			return data;
		}

		void write_16bits(uint16_t word) override
		{
			_driver.write(_device_address, reinterpret_cast<uint8_t const *>(&word), sizeof(word));
		}

		uint16_t read_16bits() override
		{
			uint16_t data = 0;
			_driver.read(_device_address, reinterpret_cast<uint8_t*>(&data), sizeof(data));
			return data;
		}
};


class I2c::Root : public Root_component<I2c::Session_component>
{
	private:

		Rpc_entrypoint   &_ep;
		I2c::Driver_base &_driver;
		Xml_node const    _config;

		/* address 0x0 is reserved, so if we return 0x0 it is an error */
		uint8_t _get_device_address(I2c::Device_name const &device_name) const
		{
			uint8_t address = 0;
			_config.for_each_sub_node([&address, &device_name] (Xml_node const &node) {
				if (node.type() == "policy") {
					I2c::Device_name const label = node.attribute_value("label_prefix", label);
					if (label == device_name) {
						address = node.attribute_value("bus_address", address);
					}
				}
			});
			return address;
		}

	protected:

		Session_component *_create_session(char const *args) override
		{
			char device_name[I2c::Device_name::capacity()] { };
			Arg_string::find_arg(args, "label").string(device_name, sizeof(device_name), "");
			uint8_t const device_address = _get_device_address(device_name);
			
			if (device_address == 0) {
				warning("Session with label '",
				        Cstring { device_name },
				        "' could not be created, no such policy");
				throw Service_denied();
			}

			return new (md_alloc()) I2c::Session_component(_ep, _driver, device_address);
		}

	public:

		Root(Rpc_entrypoint &ep, Allocator &md_alloc,
		     I2c::Driver_base &driver, Xml_node const &config)
		:
			Root_component<I2c::Session_component>(&ep, &md_alloc),
			_ep(ep), _driver(driver), _config(config)
		{ }
};

#endif /* _I2C_DRIVER__COMPONENT_H_ */
