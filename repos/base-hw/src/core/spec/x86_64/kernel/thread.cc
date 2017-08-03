/*
 * \brief   Kernel back-end for execution contexts in userland
 * \author  Martin Stein
 * \author  Reto Buerki
 * \author  Stefan Kalkowski
 * \date    2013-11-11
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <kernel/thread.h>
#include <kernel/pd.h>


void Kernel::Thread::_call_update_data_region() { }


void Kernel::Thread::_call_update_instr_region() { }


/*
 * Intel manual: 6.15 EXCEPTION AND INTERRUPT REFERENCE
 *                    Interrupt 14—Page-Fault Exception (#PF)
 */
enum {
	ERR_I = 1UL << 4,
	ERR_R = 1UL << 3,
	ERR_U = 1UL << 2,
	ERR_W = 1UL << 1,
	ERR_P = 1UL << 0,
};


void Kernel::Thread::_mmu_exception()
{
	_become_inactive(AWAITS_RESTART);
	_fault_pd     = (addr_t)_pd->platform_pd();
	_fault_addr   = Cpu::Cr2::read();
	_fault_writes = (errcode & ERR_P) && (errcode & ERR_W);

	/*
	 * Core should never raise a page-fault. If this happens, print out an
	 * error message with debug information.
	 */
	if (_pd == Kernel::core_pd())
		Genode::error("page fault in core thread (", label(), "): "
		              "ip=", Genode::Hex(ip), " fault=", Genode::Hex(_fault_addr));

	if (_pager) _pager->submit(1);
	return;
}


void Kernel::Thread::_init() { }


void Kernel::Thread::_call_update_pd() { }
