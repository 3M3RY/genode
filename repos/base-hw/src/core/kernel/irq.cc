/*
 * \brief   Kernel back-end and core front-end for user interrupts
 * \author  Martin Stein
 * \date    2013-10-28
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include "kernel.h"
#include "cpu.h"
#include "irq.h"


void Kernel::Irq::disable() const {
	cpu_pool().executing_cpu().pic().mask(_irq_nr); }


void Kernel::Irq::enable() const {
	cpu_pool().executing_cpu().pic().unmask(_irq_nr, Cpu::executing_id()); }


Kernel::Irq::Pool &Kernel::User_irq::_pool()
{
	static Irq::Pool p;
	return p;
}
