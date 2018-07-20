/*
 * \brief  Escape-sequence decoder
 * \author Norman Feske
 * \date   2011-06-06
 */

/*
 * Copyright (C) 2011-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TERMINAL__DECODER_H_
#define _TERMINAL__DECODER_H_

#include <terminal/character_screen.h>
#include <log_session/connection.h>

namespace Terminal {

	class Decoder;

	class Log_buffer : public Genode::Output
	{
		private:

			enum { BUF_SIZE = Genode::Log_session::MAX_STRING_LEN };

			char _buf[BUF_SIZE];
			unsigned _num_chars = 0;

		public:

			Log_buffer() { }

			void flush_ok()
			{
				log(Genode::Cstring(_buf, _num_chars));
				_num_chars = 0;
			}

			void flush_warning()
			{
				warning(Genode::Cstring(_buf, _num_chars));
				_num_chars = 0;
			}

			void flush_error()
			{
				error(Genode::Cstring(_buf, _num_chars));
				_num_chars = 0;
			}

			void out_char(char c) override
			{
				_buf[_num_chars++] = c;
				if (_num_chars >= sizeof(_buf))
					flush_ok();
			}

			template <typename... ARGS>
			void print(ARGS &&... args) {
				Output::out_args(*this, args...); }
	};

	struct Ascii
	{
		unsigned char const _c;

		template <typename T>
		explicit Ascii(T c) : _c(c) { }

		void print(Genode::Output &out) const
		{
			switch (_c) {
			case 000: out.out_string("NUL"); break;
			case 001: out.out_string("SOH"); break;
			case 002: out.out_string("STX"); break;
			case 003: out.out_string("ETX"); break;
			case 004: out.out_string("EOT"); break;
			case 005: out.out_string("ENQ"); break;
			case 006: out.out_string("ACK"); break;
			case 007: out.out_string("BEL"); break;
			case 010: out.out_string("BS");  break;
			case 011: out.out_string("HT");  break;
			case 012: out.out_string("LF");  break;
			case 013: out.out_string("VT");  break;
			case 014: out.out_string("FF");  break;
			case 015: out.out_string("CR");  break;
			case 016: out.out_string("SO");  break;
			case 017: out.out_string("SI");  break;
			case 020: out.out_string("DLE"); break;
			case 021: out.out_string("DC1"); break;
			case 022: out.out_string("DC2"); break;
			case 023: out.out_string("DC3"); break;
			case 024: out.out_string("DC4"); break;
			case 025: out.out_string("NAK"); break;
			case 026: out.out_string("SYN"); break;
			case 027: out.out_string("ETB"); break;
			case 030: out.out_string("CAN"); break;
			case 031: out.out_string("EM");  break;
			case 032: out.out_string("SUB"); break;
			case 033: out.out_string("ESC"); break;
			case 034: out.out_string("FS");  break;
			case 035: out.out_string("GS");  break;
			case 036: out.out_string("RS");  break;
			case 037: out.out_string("US");  break;
			case 040: out.out_string("SPACE"); break;
			case 0177: out.out_string("DEL");  break;
			default:
				if (_c & 0x80)
					Genode::Hex(_c).print(out);
				else
					out.out_char(_c);
				break;
			}
		}
	};

	struct Ecma
	{
		unsigned char const _c;

		template <typename T>
		explicit Ecma(T c) : _c(c) { }

		void print(Genode::Output &out) const
		{
			Ascii(_c).print(out);
			out.out_char('(');

			Genode::print(out, (_c)/160,    (_c>>4)%10,
			              "/", (_c&0xf)/10, (_c&0xf)%10);

			out.out_char(')');
		}
	};

}

class Terminal::Decoder
{
	private:

		/**
		 * Return digit value of character
		 */
		static inline int digit(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			return -1;
		}

		/**
		 * Return true if character is a digit
		 */
		static inline bool is_digit(char c)
		{
			return (digit(c) >= 0);
		}

		/**
		 * Return true if number starts with the specified digit
		 *
		 * \param digit  digit 0..9 to test for
		 */
		static inline bool starts_with_digit(int digit, int number)
		{
			for (; number > 9; number /= 10);
			return (number == digit);
		}

		/**
		 * Return number with the first digit removed
		 */
		static inline int remove_first_digit(int number)
		{
			int factor = 1;
			for (; number/factor > 9; factor *= 10);
			return number % factor;
		}

		enum State {
			STATE_IDLE,
			STATE_ESC_CSI,    /* read CONTROL SEQUENCE INTRODUCER */
			STATE_ESC_ECMA,   /* read an ECMA-48 escape sequence  */
			STATE_ESC_SCS,    /* read an Select Character Set sequence  */
			STATE_ESC_VT100,  /* read a VT100 escape sequence     */
			STATE_ESC_OSC    /* skip an Operating System Command */
		};

		/**
		 * Buffer used for collecting escape sequences
		 */
		class Escape_stack
		{
			private:

				Log_buffer _dump_log { };

			public:

				struct Entry
				{
					enum { INVALID, NUMBER, CODE };

					int type  = INVALID;
					int value = 0;

					void print(Genode::Output &out, State state) const
					{
						if (type == NUMBER) {
							Genode::print(out, value);
						} else if (state == STATE_ESC_ECMA) {
							Ecma(value).print(out);
						} else {
							Ascii(value).print(out);
						}
					}

					void print(Genode::Output &out) const
					{
						print(out, STATE_ESC_VT100);
					}
				};

				struct Number_entry : Entry
				{
					Number_entry(int number) { type = NUMBER, value = number; }
				};

				struct Code_entry : Entry
				{
					Code_entry(int code) { type = CODE, value = code; }
				};

				struct Invalid_entry : Entry { };

			private:

				enum { MAX_ENTRIES = 32 };
				Entry _entries[MAX_ENTRIES];
				int   _index;

				void _dump(State state)
				{
					_dump_log.print("ESC");
					for (int i = 0; i < _index; i++) {
						_dump_log.out_char(' ');
						_entries[i].print(_dump_log, state);
					}
				}

			public:

				Escape_stack() : _index(0) { }

				void reset() { _index = 0; }

				void discard(State state = STATE_ESC_VT100)
				{
					_dump_log.print("unhandled sequence ");
					_dump(state);
					_dump_log.flush_warning();
					_index = 0;
				}

				void push(Entry const &entry)
				{
					if (_index == MAX_ENTRIES - 1) {
						Genode::error("escape stack overflow");
						_dump(STATE_ESC_VT100);
						_dump_log.flush_error();
						reset();
						return;
					}
					_entries[_index++] = entry;
				}

				/**
				 * Return number of stack elements
				 */
				unsigned num_elem() const { return _index; }

				/**
				 * Return Nth stack entry
				 *
				 * 'index' is relative to the bottom of the stack.
				 */
				Entry operator [] (int index) const
				{
					return (index <= _index) ? _entries[index] : Invalid_entry();
				}

		} _escape_stack { };

		Character_screen &_screen;

		State _state = STATE_IDLE;

		int _number = -1; /* current number argument supplied in escape sequence */

		void _append_to_number(char c)
		{
			_number = (_number < 0 ? 0 : _number)*10 + digit(c);
		}

		void _enter_state_idle()
		{
			_state = STATE_IDLE;
			_escape_stack.reset();
			_number = -1;
		}

		void _enter_state_esc_csi()
		{
			_state = STATE_ESC_CSI;
			_escape_stack.reset();
		}

		void _enter_state_esc_ecma()
		{
			_state = STATE_ESC_ECMA;
		}

		void _enter_state_esc_vt100()
		{
			_state = STATE_ESC_VT100;
		}

		void _enter_state_esc_osc()
		{
			_state = STATE_ESC_OSC;
		}

		bool _sgr(int const p)
		{
			if (p < 30)
				return (_screen.sgr(p), true);

			/* p starting with digit '3' -> set foreground color */
			if (starts_with_digit(3, p))
				return (_screen.setaf(remove_first_digit(p)), true);

			/* p starting with digit '4' -> set background color */
			if (starts_with_digit(4, p))
				return (_screen.setab(remove_first_digit(p)), true);

			return false;
		}

		/**
		 * Try to handle single-element escape sequence
		 *
		 * \return true if escape sequence was handled
		 */
		bool _handle_esc_seq_1()
		{
			switch (_escape_stack[0].value) {
			case 'H': return (_screen.hts(), true);
			case 'c': return true; /* prefixes 'rs2' */
			case 'E': return (_screen.nel(), true);
			case '>': return true; /* follows 'rmkx' */
			case '=': return true; /* follows 'smkx' */
			default:  return false;
			}
		}

		bool _handle_esc_seq_2()
		{
			switch (_escape_stack[0].value) {

			case '[':
				switch (_escape_stack[1].value) {

				case 'A': return (_screen.cuu(), true);
				case 'B': return (_screen.cud(), true);
				case 'C': return (_screen.cuf(), true);
				case 'D': return (_screen.cub(), true);
				case 'G': return (_screen.cha(), true);
				case 'H': return (_screen.quad(), true);
				case 'J': return (_screen.ed(),   true);
				case 'K': return (_screen.el(),   true);
				case 'L': return (_screen.il(),  true);
				case 'M': return (_screen.dl(), true);
				case 'P': return (_screen.dch(), true);
				//case 'Z': return (_screen.cbt(),  true);
				case 'm': return _sgr(0);
				case 'S': return (_screen.su(), true);
				case 'T': return (_screen.sd(), true);
				case 'c': return (_screen.da(), true);
				case 'd': return (_screen.vpa(), true);
				case 'n': return (_screen.vpb(), true);
				case '@': return (_screen.ich(), true);
				default:  return false;
				}
				break;

			default: return false;
			}
		}

		bool _handle_esc_seq_3()
		{
			/*
			 * All three-element sequences have the form \E[<NUMBER><COMMAND>
			 */
			if ((_escape_stack[0].value != '[')
			 || (_escape_stack[1].type  != Escape_stack::Entry::NUMBER))
				return false;

			int  const p1      = _escape_stack[1].value;
			char const command = _escape_stack[2].value;

			switch (command) {
			case 'A': return (_screen.cuu(p1), true);
			case 'B': return (_screen.cud(p1), true);
			case 'C': return (_screen.cuf(p1), true);
			case 'D': return (_screen.cub(p1), true);
			case 'd': return (_screen.vpa(p1), true);
			//case 'g': return (p1 == 3) && (_screen.tbc(), true);
			case 'G': return (_screen.cha(p1), true);
			case 'J': return (_screen.ed(p1), true);
			case 'K': return (_screen.el(p1), true);
			case 'L': return (_screen.il(p1), true);
			case 'l': return (_screen.rm(p1), true);
			case 'M': return (_screen.dl(p1), true);
			case 'm': return _sgr(p1);
			case 'n': return (_screen.vpb(p1), true);
			case 'P': return (_screen.dch(p1), true);
			case '@': return (_screen.ich(p1), true);
			case 'S': return (_screen.su(p1), true);
			case 'T': return (_screen.sd(p1), true);
			case 'X': return (_screen.ech(p1), true);

			default: break;
			}
			return false;
		}

		bool _handle_esc_seq_4()
		{
			/*
			 * All four-element escape sequences have the form
			 * \E[?<NUMBER><COMMAND>
			 */
			if ((_escape_stack[0].value != '[')
			 || (_escape_stack[1].value != '?')
			 || (_escape_stack[2].type  != Escape_stack::Entry::NUMBER))
			 	return false;

			int  const p1      = _escape_stack[2].value;
			char const command = _escape_stack[3].value;

			switch (command) {
			case 'h': return (_screen.decsm(p1), true);
			case 'l': return (_screen.decrm(p1), true);
			default: break;
			}
			return false;
		}

		bool _handle_esc_seq_5()
		{
			/*
			 * All five-element escape sequences have the form
			 * \E[<NUMBER1>;<NUMBER2><COMMAND>
			 */
			if ((_escape_stack[0].value != '[')
			 || (_escape_stack[1].type  != Escape_stack::Entry::NUMBER)
			 || (_escape_stack[2].value != ';')
			 || (_escape_stack[3].type  != Escape_stack::Entry::NUMBER))
				return false;

			int const p[2]    = { _escape_stack[1].value,
			                      _escape_stack[3].value };
			int const command = _escape_stack[4].value;

			switch (command) {
			case 'r': return (_screen.csr(p[0], p[1]), true);
			case 'H': return (_screen.cup(p[0], p[1]), true);
			case 'm':

				if (p[0] == 39 && p[1] == 49)
					return (_screen.op(), true);

				for (int i = 0; i < 2; i++)
					if (!_sgr(p[i]))
						Genode::warning("Number ", p[i],
						                " in sequence '[",
						                p[0], ";",
						                p[1], "m' is not implemented");

				return true;

			default: return false;
			}
		}

		bool _handle_esc_seq_6()
		{
			/*
			 * All five-element escape sequences have the form
			 * \E[?<NUMBER1>;<NUMBER2><COMMAND>
			 */
			if ((_escape_stack[0].value != '[')
			 || (_escape_stack[1].value != '?')
			 || (_escape_stack[2].type  != Escape_stack::Entry::NUMBER)
			 || (_escape_stack[3].value != ';')
			 || (_escape_stack[4].type  != Escape_stack::Entry::NUMBER))
				return false;

			int const p[2] = { _escape_stack[2].value,
			                   _escape_stack[4].value };
			switch (_escape_stack[5].value) {
			case 'h': return (_screen.decsm(p[0], p[1]), true);
			case 'l': return (_screen.decrm(p[0], p[1]), true);
			default: return false;
			}
		}

		bool _handle_esc_seq_7()
		{
			/*
			 * All six-element escape sequences have the form
			 * \E[<NUMBER1>;<NUMBER2>;<NUMBER3><COMMAND>
			 */
			if ((_escape_stack[0].value != '[')
			 || (_escape_stack[1].type  != Escape_stack::Entry::NUMBER)
			 || (_escape_stack[2].value != ';')
			 || (_escape_stack[3].type  != Escape_stack::Entry::NUMBER)
			 || (_escape_stack[4].value != ';')
			 || (_escape_stack[5].type  != Escape_stack::Entry::NUMBER))
				return false;

			int const p[3]    = { _escape_stack[1].value,
			                      _escape_stack[3].value,
			                      _escape_stack[5].value };
			int const command = _escape_stack[6].value;

			switch (command) {
			case 'm':

				for (int i = 0; i < 3; i++)
					if (!_sgr(p[i]))
						Genode::warning("Number ", p[i],
						                " in sequence '[",
						                p[0], ";",
						                p[1], ";",
						                p[2], "m' is not implemented");

				return true;

			default: return false;
			}

			return true;
		}

		bool _complete()
		{
			return (((_escape_stack.num_elem() == 1) && _handle_esc_seq_1())
			     || ((_escape_stack.num_elem() == 2) && _handle_esc_seq_2())
			     || ((_escape_stack.num_elem() == 3) && _handle_esc_seq_3())
			     || ((_escape_stack.num_elem() == 4) && _handle_esc_seq_4())
			     || ((_escape_stack.num_elem() == 5) && _handle_esc_seq_5())
			     || ((_escape_stack.num_elem() == 6) && _handle_esc_seq_6())
			     || ((_escape_stack.num_elem() == 7) && _handle_esc_seq_7()));
		}

	public:

		Decoder(Character_screen &screen) : _screen(screen) { }

		void insert(unsigned char c)
		{
			switch (_state) {

			case STATE_IDLE:

				enum { ESC_PREFIX = 0x1b };
				if (c == ESC_PREFIX) {
					_enter_state_esc_csi();
					break;
				}

				/* handle special characters */

				/* handle normal characters */
				_screen.output(c);

				break;

			case STATE_ESC_CSI:
				/* check that the second byte is in set C1 - ECMA-48 5.3 */
				switch (c) {
				case '7':
					_screen.decsc();
					_enter_state_idle();
					break;
				case '8':
					_screen.decrc();
					_enter_state_idle();
					break;
				case '(':
				case ')':
					_escape_stack.push(Escape_stack::Code_entry(c));
					_state = STATE_ESC_SCS;
					break;
				case ']':
					_enter_state_esc_osc();
					break;
				case 'M':
					_screen.reverse_index();
					_enter_state_idle();
					break;
				default:
					if (0x40 <= c && c <= 0x5f) {
						_escape_stack.push(Escape_stack::Code_entry(c));
						_enter_state_esc_ecma();
						break;
					} 
					Genode::error("unknown CSI ESC", Ascii(c));
					_enter_state_idle();
				}

				break;

			case STATE_ESC_ECMA:
			case STATE_ESC_VT100:
				/*
				 * We received the prefix character of an escape sequence,
				 * collect the escape-sequence elements until we detect the
				 * completion of the sequence.
				 */

				/* check for start of a number argument */
				if (is_digit(c)) {
					_append_to_number(c);
				}

				else /* non-number character of escape sequence */
				{
					if (-1 < _number) {
						_escape_stack.push(Escape_stack::Number_entry(_number));
						_number = -1;
					}

					_escape_stack.push(Escape_stack::Code_entry(c));

					/* check for Final Byte - ECMA-48 5.4 */
					if (_state == STATE_ESC_ECMA && c > 0x3f && c < 0x7f) {
						if (!_complete()) {
							_escape_stack.discard(_state);
						}
						_enter_state_idle();
					} else {
						if (_complete())
							_enter_state_idle();
					}
				}
				break;

			case STATE_ESC_SCS:
				switch (_escape_stack[0].value) {
				case '(': _screen.scs_g0(c); break;
				case ')': _screen.scs_g1(c); break;
				}
				_enter_state_idle();
				break;

			case STATE_ESC_OSC:
				enum { BELL = 07 };
				_escape_stack.push(Escape_stack::Code_entry(c));
				if (c == BELL) {
					_escape_stack.discard(_state);
					_enter_state_idle();
				}

				break;
			}
		};
};

#endif /* _TERMINAL__DECODER_H_ */
