/*
 * \brief  Solo5 hardware virtualization tender
 * \author Emery Hemingway
 * \date   2018-09-16
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/debug.h>

/* Genode includes */
#include <vmm/vcpu_thread.h>
#include <block_session/connection.h>
#include <nic/packet_allocator.h>
#include <nic_session/connection.h>
#include <rtc_session/connection.h>
#include <timer_session/connection.h>
#include <log_session/connection.h>
#include <pd_session/connection.h>
#include <base/attached_rom_dataspace.h>
#include <base/attached_ram_dataspace.h>
#include <base/heap.h>
#include <base/component.h>
#include <base/sleep.h>
#include <util/touch.h>

/* VMM utility includes */
#include <vmm/guest_memory.h>
#include <vmm/vcpu_thread.h>
#include <vmm/vcpu_dispatcher.h>
#include <vmm/utcb_guard.h>

/* NOVA includes that come with Genode */
#include <nova/syscalls.h>
#include <nova/print.h>

extern "C" {
/* Upstream HVT includes */
#include "hvt.h"
#include "hvt_cpu_x86_64.h"
}

//#include "devices.h"

namespace Hvt {
	typedef Genode::Hex_range<unsigned long> Hex_range;

	using namespace Genode;
	using Genode::uint64_t;

	/* used to create 2 MiB mappings */
	enum {
		GUEST_PAGE_MASK = ~(X86_GUEST_PAGE_SIZE-1),
		GUEST_PAGE_ORDER = 9
	};

	struct _Guest_memory;
	struct Vcpu_handler;
	struct Main;

};


/* local includes */
#include "guest_memory.h"


struct Hvt::Vcpu_handler : Vmm::Vcpu_dispatcher<Genode::Thread>
{
		Guest_memory      &_guest_memory;
		Vmm::Vcpu_same_pd  _vcpu_thread;
		addr_t             _guest_entry = 0xfff0;

		static Nova::Utcb &_utcb_of_myself()
		{
			return *reinterpret_cast<Nova::Utcb *>(
				Genode::Thread::myself()->utcb());
		}

		void _vcpu_init(Nova::Utcb &utcb)
		{
			/* From the AMD manual */
			using namespace Nova;

			memset(&utcb, 0x00, Utcb::size());
			utcb.mtd = 0xfffff;

			utcb.cr0 = 0x10;
			utcb.flags = 0x2;

			utcb.ip = 0xfff0;

			utcb.cs.sel   = 0xf000;
			utcb.cs.base  = 0xffff0000;
			utcb.cs.limit = 0xffff;
			utcb.cs.ar    = 0x9a;

			utcb.ds.limit =
			utcb.es.limit =
			utcb.fs.limit =
			utcb.gs.limit =
			utcb.ss.limit = 0xffff;

			utcb.ds.ar =
			utcb.es.ar =
			utcb.fs.ar =
			utcb.gs.ar =
			utcb.ss.ar = 0x92;

			utcb.gdtr.limit =
			utcb.idtr.limit = 0xffff;

			utcb.ldtr.ar = 0x82;
			utcb.ldtr.limit = 0xffff;

			utcb.tr.ar      = 0x83;
			utcb.tr.limit   = 0xffff;

			// processor info
			// utcb.dx = 0x600;

			utcb.dr7 = 0x00000400;
		}

		void _vcpu_hvt_init(Nova::Utcb &utcb)
		{
			using namespace Nova;

			_vcpu_init(utcb);

			utcb.mtd |= 0
				| Mtd::EBSD
				| Mtd::ESP
				| Mtd::EIP
				| Mtd::EFL
				| Mtd::ESDS
				| Mtd::FSGS
				| Mtd::CSSS
				| Mtd::TR
				| Mtd::LDTR
				| Mtd::GDTR
				| Mtd::IDTR
				| Mtd::CR;

			utcb.ip    = _guest_memory.gp_entry();
			utcb.flags = X86_RFLAGS_INIT;
			utcb.sp    = _guest_memory.size() - 8;
			utcb.di    = X86_BOOT_INFO_BASE;

			// Basic CPU control in CR0
			utcb.cr0 = X86_CR0_INIT; // ^ X86_CR0_PG;

			// PML4
			utcb.cr3   = X86_CR3_INIT;

			// Intel CPU features in CR4
			utcb.cr4   = X86_CR4_INIT;

			// Long-mode
			utcb.efer  = X86_EFER_INIT;

			/**
			 * Convert from HVT x86_sreg to NOVA format
			 */
			#define write_sreg(nova, hvt) \
				nova.sel = hvt.selector;  \
				nova.ar  = 0              \
					| hvt.type     << 0   \
					| hvt.s        << 4   \
					| hvt.dpl      << 5   \
					| hvt.p        << 7   \
					| hvt.avl      << 8   \
					| hvt.l        << 9   \
					| hvt.db       << 10  \
					| hvt.g        << 11  \
					| hvt.unusable << 12; \
				nova.limit = hvt.limit;   \
				nova.base  = hvt.base;    \

			write_sreg(utcb.cs,   hvt_x86_sreg_code);
			write_sreg(utcb.es,   hvt_x86_sreg_data);
			write_sreg(utcb.ss,   hvt_x86_sreg_data);
			write_sreg(utcb.ds,   hvt_x86_sreg_data);
			write_sreg(utcb.fs,   hvt_x86_sreg_data);
			write_sreg(utcb.gs,   hvt_x86_sreg_data);
			write_sreg(utcb.ldtr, hvt_x86_sreg_unusable);
			write_sreg(utcb.tr,   hvt_x86_sreg_tr);

			#undef set_utcb_sreg

			utcb.gdtr.limit = X86_GDTR_LIMIT;
			utcb.gdtr.base  = X86_GDT_BASE;
		}

		void _svm_startup()
		{
			using namespace Nova;

			Utcb &utcb = _utcb_of_myself();
			_vcpu_hvt_init(utcb);
		}

		/*
		 * VirtualBox stores segment attributes in Intel format using a 32-bit
 		* value. NOVA represents the attributes in packed format using a 16-bit
		 * value.
		 */
		static inline Genode::uint16_t sel_ar_conv_to_nova(Genode::uint32_t v)
		{
			return (v & 0xff) | ((v & 0x1f000) >> 4);
		}


		static inline Genode::uint32_t sel_ar_conv_from_nova(Genode::uint16_t v)
		{
			return (v & 0xff) | (((uint32_t )v << 4) & 0x1f000);
		}

		char _utcb_backup[Nova::Utcb::size()];

		void _dump_utcb_backup()
		{
			Nova::Utcb &utcb = *((Nova::Utcb *)_utcb_backup);
			Vmm::log("--- _vmx_startup UTCB ---\n", utcb);
		}

		void _handle_debug_timeout(Duration)
		{
			//_dump_utcb_backup;

			Nova::ec_ctrl(Nova::EC_RECALL, ec_sel());
		}

		void _vmx_triple()
		{
			using namespace Nova;

			Vmm::error(__func__);

			//_dump_utcb_backup();

    		struct x86_gdt_desc *gdt = (struct x86_gdt_desc *)
				(_guest_memory.local_base() + X86_GDT_BASE);
    		uint64_t *raw = (uint64_t *)
				(_guest_memory.local_base() + X86_GDT_BASE);

			for (unsigned i = 0; i < X86_GDT_MAX; ++i) {
				log("GDT ", i, ": ", (Hex)raw[i]);
				log("       type=", (Hex)gdt[i].type,
					" base=", (Hex)((gdt[i].base_hi<<24) | (gdt[i].base_lo)));
			}

			throw Exception();
		}

		void _vmx_invalid()
		{
			using namespace Nova;

			Vmm::error(__func__);
			_handle_debug_timeout(Duration(Microseconds(0)));

			Utcb &utcb = _utcb_of_myself();

			unsigned const dubious = utcb.inj_info |
			                         utcb.intr_state | utcb.actv_state;
			if (dubious)
				Vmm::warning(__func__, " - dubious -"
				             " inj_info=", Genode::Hex(utcb.inj_info),
				             " inj_error=", Genode::Hex(utcb.inj_error),
				             " intr_state=", Genode::Hex(utcb.intr_state),
				             " actv_state=", Genode::Hex(utcb.actv_state));

			{
				Nova::Utcb &u = *((Nova::Utcb *)_utcb_backup);
				log(u);
			}

			//_dump_utcb_backup();
			throw Exception();
		}

		void _vmx_startup()
		{
			Nova::Utcb &utcb = _utcb_of_myself();
			_vcpu_hvt_init(utcb);
		}

		void _svm_npt()
		{
			using namespace Nova;
			Nova::Utcb &utcb = _utcb_of_myself();

			addr_t const gpa = utcb.qual[1];
			addr_t const ip  = utcb.ip;
			if (gpa > _guest_memory.size()) {
				Vmm::error("guest attemped to access ", Hex(gpa), " which is beyond ", Hex(_guest_memory.size()));
				utcb.mtd |= Mtd::CTRL;
				utcb.ctrl[0] = 1 << 18; /* cpuid */
				utcb.ctrl[1] = 1 << 0;  /* vmrun */
				return;
			}

			utcb.set_msg_word(0);
			for (addr_t boundry = 0;
			     boundry < _guest_memory.local_size();
			     boundry += X86_GUEST_PAGE_SIZE)
			{
				Nova::Mem_crd crd(
					(_guest_memory.local_addr()+boundry) >> PAGE_SIZE_LOG2,
					GUEST_PAGE_ORDER, Nova::Rights(true, true, true));
				if (!utcb.append_item(crd, boundry, false, true)) {
					break;
				}
			}
		}

		void _vmx_ept()
		{
			Nova::Utcb &utcb = _utcb_of_myself();

			addr_t const phys_addr = utcb.qual[1];

			Vmm::log(__func__, " ", (Hex)phys_addr);
		}

		void _svm_exception()
		{
			Vmm::error(__func__, " not handled");
		}

		void _analyze_page_table(addr_t cr3, addr_t gpa)
		{
			enum { ACCESS = 1 << 5 };
			log(__func__, ": page fault adress is ", (Hex)gpa);

			log("PML4 is at ", Hex(cr3));

			addr_t plm4i = (gpa >> 39);
			log("PML4 index is ", plm4i, " ");

			uint64_t *pml4 = (uint64_t *)cr3;
			log("PML4E: ", Hex(pml4[plm4i]));
			if (pml4[plm4i] & ACCESS)
				log("PML4E has been accessed");

			log("PDPT is at ", Hex(pml4[plm4i] & (~0U<<12)));
			uint64_t *pdpt = (uint64_t *)(pml4[plm4i] & (~0U<<12));

			addr_t pdpti = (gpa >> 30);
			log("Page directory pointer index is ", pdpti);

			log("PDPTE: ", Hex(pdpt[pdpti]));
			if (pdpt[pdpti] & ACCESS)
				log("PDPTE has been accessed");


			log("PDT is at ", Hex(pdpt[pdpti] & (~0U<<12)));
			uint64_t *pdt = (uint64_t *)(pdpt[pdpti] & (~0U<<12));

			addr_t pdi  = (gpa >> 21);
			log("Page directory index is ", pdi);

			log("PDE: ", Hex(pdt[pdi]));
			if (pdt[pdi] & ACCESS)
				log("PDE has been accessed");

			log("Physical page is at ", Hex(pdt[pdi] & (~0U<<20)));
			
			addr_t offset = gpa & ((1<< 21)-1);
			log("Page byte offset is ", (Hex)offset);
		}

		void _svm_triple()
		{
			{
				Vmm::Utcb_guard::Utcb_backup backup_utcb;
				Vmm::Utcb_guard guard(backup_utcb);

				Nova::Utcb &utcb = *reinterpret_cast<Nova::Utcb *>(&backup_utcb);

				error("SVM triple fault exit");
				error("        ip=", (Hex)utcb.ip);
				error("   qual[0]=", (Hex)utcb.qual[0]);
				error("   qual[1]=", (Hex)utcb.qual[1]);
				error("intr_state=", (Hex)utcb.intr_state);
				error("actv_state=", (Hex)utcb.actv_state);
				error(  "inj_info=", (Hex)utcb.inj_info);
				error( "inj_error=", (Hex)utcb.inj_error);
				error( "inj_info.vector=", utcb.inj_info & 0x7f);

				if (utcb.inj_info & (1<<11)) {
					error("guest exception would have pushed an error code");
				}

				if (utcb.inj_info & (1<<31)) {
					error("intercept occured while guest attempted to deliver an exception through the IDT");
				}

				if ((utcb.inj_info & 0x7f) == 14) {
					addr_t pfa = utcb.cr2;
					_analyze_page_table(utcb.cr3, pfa);
				}

				sleep_forever();
			}

			Nova::Utcb &utcb = _utcb_of_myself();
			utcb.mtd = Nova::Mtd::INJ;
			utcb.inj_info = 0;
		}

		void _svm_io()
		{
			using namespace Nova;

			Utcb &utcb = _utcb_of_myself();
			unsigned io_order = utcb.qual[0] & 1;
			unsigned port     = utcb.qual[0] >> 16;

			Vmm::log(__func__, ": ip=", (Hex)utcb.ip, " order=", (Hex)io_order, " port=", (Hex)port, " hypecall=", port-HVT_HYPERCALL_PIO_BASE, " (", Hex(utcb.qual[0]), ")");

			//void _handle_io(bool is_in, unsigned io_order, unsigned port)
			throw Exception();
		}

		/**
		 * Shortcut for calling 'Vmm::Vcpu_dispatcher::register_handler'
		 * with 'Vcpu_dispatcher' as template argument
		 */
		template <unsigned EV, void (Vcpu_handler::*FUNC)()>
		void _register_handler(Genode::addr_t exc_base, Nova::Mtd mtd)
		{
			register_handler<EV, Vcpu_handler, FUNC>(exc_base, mtd);
		}

		addr_t ec_sel() {
			return sel_sm_ec() + 1; }

		enum { STACK_SIZE = 1024*sizeof(long) };

		Vcpu_handler(Genode::Env &env, Guest_memory &memory)
		:
			Vmm::Vcpu_dispatcher<Genode::Thread>(
				env, STACK_SIZE, env.cpu(), Affinity::Location()),
			_guest_memory(memory),
			_vcpu_thread(env, env.cpu(),
			             Affinity::Location(),
			             env.pd_session_cap(),
			             1024*sizeof(Genode::addr_t))
		{
			using namespace Nova;

			/* detect virtualization extension */
			Attached_rom_dataspace const info(env, "platform_info");
			Genode::Xml_node const hardware = info.xml().sub_node("hardware");

			hardware.sub_node("tsc").attribute("freq_khz").value(
				&_guest_memory.boot_info().cpu.tsc_freq);
			_guest_memory.boot_info().cpu.tsc_freq *= 1000;

			Genode::Xml_node const features = hardware.sub_node("features");
			bool const has_svm = features.attribute_value("svm", false);
			bool const has_vmx = features.attribute_value("vmx", false);

			addr_t const exc_base = _vcpu_thread.exc_base();

			/* register virtualization event handlers */
			if (has_svm) {
				log("SVM detected");

				_register_handler<0x7f, &Vcpu_handler::_svm_triple>
					(exc_base, Mtd::ALL);

				_register_handler<0xfc, &Vcpu_handler::_svm_npt>
					(exc_base, Mtd::CTRL);

				_register_handler<0xfe, &Vcpu_handler::_svm_startup>
					(exc_base, Mtd::ALL);

		/*
			_register_handler<0x7b, &Vcpu_handler::_svm_io>
					(exc_base, Mtd::ALL);


				_register_handler<0x4e, &Vcpu_handler::_svm_exception>
					(exc_base, Mtd::ALL);

				_register_handler<0x67, &Vcpu_handler::_gdt_access>
					(exc_base, Mtd::ALL);

				_register_handler<0x6b, &Vcpu_handler::_gdt_access>
					(exc_base, Mtd::ALL);

				_register_handler<0xff, &Vcpu_handler::_recall>
					(exc_base, Mtd::ALL);
	*/

			} else if (has_vmx) {
				log("VMX detected");

				_register_handler<0x02, &Vcpu_handler::_vmx_triple>
					(exc_base, Mtd::ALL);

				_register_handler<0x21, &Vcpu_handler::_vmx_invalid>
					(exc_base, Mtd::ALL);

				_register_handler<0x30, &Vcpu_handler::_vmx_ept>
					(exc_base, Mtd::ALL);

				_register_handler<0xfe, &Vcpu_handler::_vmx_startup>
					(exc_base, 0);

			} else {
				error("no hardware virtualization extensions available");
				throw Exception();
			}

			log("eager map memory");
			env.pd().map(_guest_memory.local_addr(), _guest_memory.local_size());
		}

		void start()
		{
			log("start virtual CPU");
			_vcpu_thread.start(ec_sel());
		}
		
};


struct Hvt::Main
{
	Genode::Env &_env;

	//Devices _devices { _env };

	Guest_memory _guest_memory { _env, X86_GUEST_PAGE_SIZE*3 };

	Vcpu_handler _vpu_handler { _env, _guest_memory };

	Main(Genode::Env &env)
	: _env(env)
	{
		char *cmdline = (char *)(_guest_memory.local_base()+X86_CMDLINE_BASE);
		Genode::snprintf(cmdline, HVT_CMDLINE_SIZE, "NOVA");
	}

	void start() { _vpu_handler.start(); }
};

extern unsigned long _prog_img_beg;  /* begin of program image (link address) */
extern unsigned long _prog_img_end;  /* end of program image */

void Component::construct(Genode::Env &env)
{
	using namespace Genode;

	/* reserve the guest physical address space */
	static Vmm::Virtual_reservation reservation(
		env, 512*X86_GUEST_PAGE_SIZE);

	static Hvt::Main main(env);

	/* diagnostic messages */
	log(Hvt::Hex_range(0, 512*X86_GUEST_PAGE_SIZE),
	            " - Solo5 physical memory");

	log(Hvt::Hex_range(main._guest_memory.local_addr(), main._guest_memory.local_size()),
	            " - HVT shadow mapping");

	log(Hvt::Hex_range((Genode::addr_t)&_prog_img_beg,
	                      (Genode::addr_t)&_prog_img_end -
	                      (Genode::addr_t)&_prog_img_beg),
	            " - HVT program image");

	log(Hvt::Hex_range(Genode::Thread::stack_area_virtual_base(),
	                      Genode::Thread::stack_area_virtual_size()),
	            " - Genode stack area");

	main.start();
}


extern "C"
void __assert(const char *, const char *file, int line, const char *failedexpr)
{
	Genode::error("Assertion failed: (", failedexpr, ") ", file, ":", line);
	Genode::sleep_forever();
};
