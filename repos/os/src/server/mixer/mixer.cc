/*
 * \brief  Audio_out Mixer
 * \author Sebastian Sumpf
 * \author Josef Soentgen
 * \author Emery Hemingway
 * \date   2012-12-20
 *
 * The mixer implements the audio session on the server side. For each channel
 * (currently 'left' and 'right' only) it supports multiple client sessions and
 * mixes all input sessions down to a single audio input service.
 *
 *
 * There is a session space (Mixer::Session_channel) for each output channel that
 * contains multiple input sessions (Audio_out::Session_elem). For every packet
 * in the output queue the mixer sums the corresponding packets from all input
 * sessions up. The volume level of an input packet is applied in a linear way
 * (sample_value * volume_level) and the output packet is clipped at [1.0,-1.0].
 */

/*
 * Copyright (C) 2009-2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/debug.h>

/* Genode includes */
#include <os/session_requests.h>
#include <mixer/channel.h>
#include <os/reporter.h>
#include <root/component.h>
#include <util/retry.h>
#include <util/string.h>
#include <util/xml_node.h>
#include <audio_in_session/capability.h>
#include <audio_in_session/rpc_object.h>
#include <audio_out_session/rpc_object.h>
#include <timer_session/connection.h>
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/component.h>
#include <base/log.h>

typedef Mixer::Channel Channel;


enum {
	LEFT         = Channel::Number::LEFT,
	RIGHT        = Channel::Number::RIGHT,
	MAX_CHANNELS = Channel::Number::MAX_CHANNELS,
	MAX_VOLUME   = Channel::Volume_level::MAX,
	MAX_CHANNEL_NAME_LEN = 16,
	INVALID_ID           = ~0UL,
};


static struct Names {
	char const      *name;
	Channel::Number  number;
} names[] = {
	{ "left",        (Channel::Number)LEFT },
	{ "front left",  (Channel::Number)LEFT },
	{ "right",       (Channel::Number)RIGHT },
	{ "front right", (Channel::Number)RIGHT },
	{ nullptr,       (Channel::Number)MAX_CHANNELS }
};


static Channel::Number number_from_string(char const *name)
{
	for (Names *n = names; n->name; ++n)
		if (!Genode::strcmp(name, n->name))
			return n->number;
	return Channel::Number::INVALID;
}


static char const *string_from_number(Channel::Number ch)
{
	for (Names *n = names; n->name; ++n)
		if (ch == n->number)
			return n->name;
	return nullptr;
}


/**
 * Helper method for walking over arrays
 */
template <typename FUNC>
static void for_each_index(int max_index, FUNC const &func) {
	for (int i = 0; i < max_index; i++) func(i); }


/****************************************************
 ** Audio_in session implementation (mixer output) **
 ****************************************************/

namespace Audio_in { struct Session_component; }

struct Audio_in::Session_component final : Audio_in::Session_rpc_object
{
	Genode::Parent::Server::Id id { INVALID_ID };

	Genode::Session_label label { };

	Session_component(Genode::Env &env, Genode::Signal_context_capability data_cap)
	: Audio_in::Session_rpc_object(env, data_cap) { }
};


namespace Audio_out
{
	class Session_elem;
	typedef Genode::Id_space<Session_elem> Session_space;
	typedef Session_space Session_channel;
	typedef Session_space::Id Session_id;

	class Session_component;
	class Root;
	class Mixer;
}


/**
 * The actual session element
 *
 * It is part of the Audio_out::Session_component implementation but since
 * it is also used by the mixer we defined it here.
 */
struct Audio_out::Session_elem : Audio_out::Session_rpc_object
{
	Session_space::Element const session_elem;
	Session_space::Element const channel_elem;

	Genode::Session_label const label;

	Channel::Number number { Channel::INVALID };
	float           volume { 0.f };
	bool            muted  { true };

	Session_elem(Genode::Env & env,
	             Session_space &session_space, Session_space &channel_space,
	             Session_id &id, Genode::Session_label const &label,
	             Genode::Signal_context_capability data_cap)
	: Session_rpc_object(env, data_cap),
	  session_elem(*this, session_space, id),
	  channel_elem(*this, channel_space, id),
	  label(label)
	{ }

	Packet *get_packet(unsigned offset) {
		return stream()->get(stream()->pos() + offset); }
};


/**
 * The mixer
 */
class Audio_out::Mixer
{
	private:

		/*
		 * Noncopyable
		 */
		Mixer(Mixer const &);
		Mixer &operator = (Mixer const &);

		Genode::Env &env;

		Genode::Attached_rom_dataspace _config_rom { env, "config" };

		struct Verbose
		{
			bool const sessions;
			bool const changes;

			Verbose(Genode::Xml_node config)
			:
				sessions(config.attribute_value("verbose_sessions", false)),
				changes(config.attribute_value("verbose_changes", false))
			{ }
		};

		Genode::Reconstructible<Verbose> _verbose { _config_rom.xml() };

		/*
		 * Signal handlers
		 */
		Genode::Signal_handler<Audio_out::Mixer> _handler
			{ env.ep(), *this, &Audio_out::Mixer::_handle };

		Genode::Signal_handler<Audio_out::Mixer> _handler_config
			{ env.ep(), *this, &Audio_out::Mixer::_handle_config_update  };

		Genode::Signal_context_capability _data_cap { };

		/*
		 * Audio_in RPC objects for mixer output
		 */
		Audio_in::Session_component _left  { env, _data_cap };
		Audio_in::Session_component _right { env, _data_cap };

		Audio_in::Session_component *_out[MAX_CHANNELS];
		float                        _out_volume[MAX_CHANNELS];

		/*
		 * Default settings used as fallback for new sessions
		 */
		float _default_out_volume { 0.f };
		float _default_volume     { 0.f };
		bool  _default_muted      { true };

		/**
		 * Remix all exception
		 */
		struct Remix_all { };

		/**
		 * A channel is a space of multiple session components
		 */
		Session_space _channels[MAX_CHANNELS];

		/**
		 * Helper method for walking over session in a channel
		 */
		template <typename FUNC> void _for_each_channel(FUNC const &func)
		{
			for (int i = LEFT; i < (int)MAX_CHANNELS; i++)
				func((Channel::Number)i, &_channels[i]);
		}

		/*
		 * Channel reporter
		 *
		 * Each session in a channel is reported as an input node.
		 */
		Genode::Reporter _reporter { env, "channel_list" };

		/**
		 * Report available channels
		 *
		 * This method is called if a new session is added or an old one
		 * removed as well as when the mixer configuration changes.
		 */
		void _report_channels()
		{
			try {
				Genode::Reporter::Xml_generator xml(_reporter, [&] () {
					/* output channels */
					for_each_index(MAX_CHANNELS, [&] (int const i) {
						Channel::Number const num = (Channel::Number)i;
						char const * const   name = string_from_number(num);
						int const             vol = (int)(MAX_VOLUME * _out_volume[i]);

						xml.node("channel", [&] () {
							xml.attribute("type",  "output");
							xml.attribute("label",  _out[i]->label);
							xml.attribute("name",   name);
							xml.attribute("number", (int)num);
							xml.attribute("volume", (int)vol);
							xml.attribute("muted",  false);
						});
					});

					/* input channels */
					_for_each_channel([&] (Channel::Number num, Session_channel *sc) {
						sc->for_each<Session_elem&>([&] (Session_elem const &session) {
							char const * const name = string_from_number(num);
							int const           vol = (int)(MAX_VOLUME * session.volume);

							xml.node("channel", [&] () {
								xml.attribute("type",   "input");
								xml.attribute("label",  session.label.string());
								xml.attribute("name",   name);
								xml.attribute("number", session.number);
								xml.attribute("active", session.active());
								xml.attribute("volume", (int)vol);
								xml.attribute("muted",  session.muted);
							});
						});
					});
				});
			} catch (...) { Genode::warning("could report current channels"); }
		}

		/*
		 * Check if any of the available session is currently active, i.e.,
		 * playing.
		 */
		bool _check_active()
		{
			bool active = false;
			_for_each_channel([&] (Channel::Number, Session_channel *sc) {
				sc->for_each<Session_elem&>([&] (Session_elem const &session) {
					active |= session.active();
				});
			});
			return active;
		}

		/*
		 * Advance the stream of the session to a new position
		 */
		void _advance_session(Session_elem *session, unsigned pos)
		{
			if (session->stopped()) return;

			Stream *stream  = session->stream();
			bool const full = stream->full();

			/* mark packets as played and icrement position pointer */
			while (stream->pos() != pos) {
				stream->get(stream->pos())->mark_as_played();
				stream->increment_position();
			}

			session->progress_submit();

			if (full) session->alloc_submit();
		}

		/*
		 * Advance the position of each session to match the output position
		 */
		void _advance_position()
		{
			for_each_index(MAX_CHANNELS, [&] (int i) {
				unsigned const pos = _out[i]->stream()->pos();
				_channels[i].for_each<Session_elem&>([&] (Session_elem &session) {
					_advance_session(&session, pos);
				});
			});
		}

		/*
		 * Mix input packet into output packet
		 *
		 * Packets are mixed in a linear way with min/max clipping.
		 */
		void _mix_packet(Audio_in::Packet *out, Audio_out::Packet *in,
		                 bool clear, float const out_vol, float const vol)
		{
			if (clear) {
				for_each_index(Audio_out::PERIOD, [&] (int const i) {
					out->content()[i]  = (in->content()[i] * vol);

					if (out->content()[i] > 1) out->content()[i] = 1;
					if (out->content()[i] < -1) out->content()[i] = -1;

					out->content()[i] *= out_vol;
				});
			} else {
				for_each_index(Audio_out::PERIOD, [&] (int const i) {
					out->content()[i] += (in->content()[i] * vol);

					if (out->content()[i] > 1) out->content()[i] = 1;
					if (out->content()[i] < -1) out->content()[i] = -1;

					out->content()[i] *= out_vol;
				});
			}

			/* mark the packet as processed by invalidating it */
			in->invalidate();
		}

		/*
		 * Mix all session of one channel
		 */
		bool _mix_channel(bool remix, Channel::Number nr, unsigned out_pos, unsigned offset)
		{
			Audio_in::Stream * const stream  = _out[nr]->stream();
			Audio_in::Packet * const out     = stream->get(out_pos + offset);
			Session_channel  * const sc      = &_channels[nr];

			float const out_vol  = _out_volume[nr];

			bool       clear     = true;
			bool       mix_all   = remix;
			bool const out_valid = out->valid();

			Genode::retry<Remix_all>(
				/*
				 * Mix the input packet at the given position of every input
				 * session to one output packet.
				 */
				[&] {
					sc->for_each<Session_elem&>([&] (Session_elem &session) {
						if (session.stopped() || session.muted || session.volume < 0.01f)
							return;

						Packet *in = session.get_packet(offset);

						/* remix again if input has changed for already mixed packet */
						if (in->valid() && out_valid && !mix_all) throw Remix_all();

						/* skip if packet has been processed or was already played */
						if ((!in->valid() && !mix_all) || in->played()) return;

						_mix_packet(out, in, clear, out_vol, session.volume);

						clear = false;
					});
				},
				/*
				 * An input packet of an already mixed output packet has
				 * changed, we have to remix all input packets again.
				 */
				[&] {
					clear   = true;
					mix_all = true;
				});

			return !clear;
		}

		/*
		 * Mix input packets
		 *
		 * \param remix force remix of already mixed packets
		 */
		void _mix(bool remix = false)
		{
			unsigned pos[MAX_CHANNELS];
			pos[LEFT]  = _out[LEFT]->stream()->pos();
			pos[RIGHT] = _out[RIGHT]->stream()->pos();

			/*
			 * Look for packets that are valid and mix channels in an alternating
			 * way.
			 */
			for_each_index(Audio_out::QUEUE_SIZE, [&] (int const i) {
				bool mix_one = true;
				for_each_index(MAX_CHANNELS, [&] (int const j) {
					mix_one = _mix_channel(remix, (Channel::Number)j, pos[j], i);
				});

				/* all channels mixed, submit to output queue */
				if (mix_one)
					for_each_index(MAX_CHANNELS, [&] (int const j) {
						Audio_in::Packet *p = _out[j]->stream()->get(pos[j] + i);
						_out[j]->stream()->submit(p);
					});
			});
		}

		/**
		 * Handle progress signals from Audio_out session and data available signals
		 * from each mixer client
		 */
		void _handle()
		{
			_advance_position();
			_mix();
		}

		/**
		 * Set default values for various options
		 */
		void _set_default_config(Genode::Xml_node const &node)
		{
			using namespace Genode;

			try {
				Xml_node default_node = node.sub_node("default");
				long v = 0;

				v = default_node.attribute_value<long>("out_volume", 0);
				_default_out_volume = (float)v / MAX_VOLUME;

				v = default_node.attribute_value<long>("volume", 0);
				_default_volume = (float)v / MAX_VOLUME;

				v = default_node.attribute_value<bool>("muted", false);
				_default_muted = v ;

				if (_verbose->changes) {
					log("Set default "
					     "out_volume: ", (int)(MAX_VOLUME*_default_out_volume), " "
					     "volume: ",     (int)(MAX_VOLUME*_default_volume), " "
					     "muted: ",      _default_muted);
				}

			} catch (...) { Genode::warning("could not read mixer default values"); }
		}

		/**
		 * Handle ROM update signals
		 */
		void _handle_config_update()
		{
			using namespace Genode;

			_config_rom.update();

			Xml_node config_node = _config_rom.xml();
			_verbose.construct(config_node);

			_set_default_config(config_node);

			/* reset out volume in case there is no 'channel_list' node */
			_out_volume[LEFT]  = _default_out_volume;
			_out_volume[RIGHT] = _default_out_volume;

			try {
				Xml_node channel_list_node = config_node.sub_node("channel_list");

				channel_list_node.for_each_sub_node([&] (Xml_node const &node) {
					Channel ch(node);

					if (ch.type == Channel::Type::INPUT) {
						_for_each_channel([&] (Channel::Number, Session_channel *sc) {

							sc->for_each<Session_elem&>([&] (Session_elem &session) {
								if (session.number != ch.number) return;
								if (session.label != ch.label) return;

								session.volume = (float)ch.volume / MAX_VOLUME;
								session.muted  = ch.muted;

								if (_verbose->changes) {
									log("Set label: '", ch.label, "' "
									    "channel: '",   string_from_number(ch.number), "' "
									    "nr: ",         (int)ch.number, " "
									    "volume: ",     (int)(MAX_VOLUME*session.volume), " "
									    "muted: ",      ch.muted);
								}
							});
						});
					}
					else if (ch.type == Channel::Type::OUTPUT) {
						for_each_index(MAX_CHANNELS, [&] (int const i) {
							if (ch.number != i) return;

							_out_volume[i] = (float)ch.volume / MAX_VOLUME;

							if (_verbose->changes) {
								log("Set label: 'master' "
								    "channel: '", string_from_number(ch.number), "' "
								    "nr: ",       (int)ch.number, " "
								    "volume: ",   (int)(MAX_VOLUME*_out_volume[i]), " "
								    "muted: ",    ch.muted);
							}
						});
					}
				});
			} catch (Xml_node::Nonexistent_sub_node) {
				Genode::warning("channel_list node missing");
			}

			/*
			 * Report back any changes so a front-end can update its state
			 */
			_report_channels();

			/*
			 * The configuration has changed, remix already mixed packets
			 * in the mixer output queue.
			 */
			_mix(true);
		}

	public:

		/**
		 * Constructor
		 */
		Mixer(Genode::Env &env) : env(env)
		{
			_reporter.enabled(true);

			_out[LEFT]  = &_left;
			_out[RIGHT] = &_right;

			_out_volume[LEFT]  = _default_out_volume;
			_out_volume[RIGHT] = _default_out_volume;

			_config_rom.sigh(_handler_config);
			_handle_config_update();
		}

		/**
		 * Deliver an Audio_in capability for an output channel
		 *
		 * \throw Service_denied
		 */
		void deliver_output(Genode::Env &env,
		                    Genode::Parent::Server::Id id,
		                    Genode::Session_label const &label,
		                    Channel::Number channel)
		{
			/* assert the channel is valid and not acquired */
			if ((Channel::Number)MAX_CHANNELS < channel
			 || _out[channel]->id.value != INVALID_ID)
				throw Genode::Service_denied();

			Audio_in::Session_component &session = *_out[channel];

			Audio_in::Session_capability cap = env.ep().manage(session);
			session.id = id;
			session.label = label;
			env.parent().deliver_session_cap(id, cap);
		}

		/**
		 * Dissolve the Audio_in RPC object for an output channel
		 */
		void close_output(Genode::Env &env, Genode::Parent::Server::Id id)
		{
			/* lookup id to channel */
			for_each_index(MAX_CHANNELS, [&] (int channel) {
				if (_out[channel]->id == id) {
					Audio_in::Session_component &session = *_out[channel];
					env.ep().dissolve(session);
					session.id.value = INVALID_ID;
					session.label = Genode::Session_label();
					env.parent().session_response(id, Genode::Parent::SESSION_CLOSED);
				}
			});
		}

		/**
		 * Start output stream
		 */
		void start()
		{
			_out[LEFT]->progress_sigh(_handler);
			for_each_index(MAX_CHANNELS, [&] (int const i) { _out[i]->start(); });
		}

		/**
		 * Stop output stream
		 */
		void stop()
		{
			for_each_index(MAX_CHANNELS, [&] (int const i) { _out[i]->stop(); });
			_out[LEFT]->progress_sigh(Genode::Signal_context_capability());
		}

		/**
		 * Get current playback position of output stream
		 */
		unsigned pos(Channel::Number channel) const { return _out[channel]->stream()->pos(); }

		/**
		 * Add input session
		 */
		void add_session(Channel::Number ch, Session_elem &session)
		{
			session.volume = _default_volume;
			session.muted  = _default_muted;

			if (_verbose->sessions) {
				log("Add label: '", session.label, "' "
				    "channel: '",   string_from_number(ch), "' "
				    "nr: ",          (int)ch, " "
				    "volume: ",      (int)(MAX_VOLUME*session.volume), " "
				    "muted: ",     session.muted);
			}

			_report_channels();
		}

		/**
		 * Remove input session
		 */
		void remove_session(Channel::Number ch, Session_elem &session)
		{
			if (_verbose->sessions) {
				log("Remove label: '", session.label, "' "
				    "channel: '", string_from_number(ch), "' "
				    "nr: ", (int)ch);
			}

			_report_channels();
		}
		
		Session_space &channel_space(Channel::Number n)
		{
			return _channels[n];
		}

		/**
		 * Get signal context that handles data avaiable as well as progress signal
		 */
		Genode::Signal_context_capability sig_cap() { return _handler; }

		/**
		 * Report current channels
		 */
		void report_channels() { _report_channels(); }
};


/**************************************
 ** Audio_out session implementation **
 **************************************/

class Audio_out::Session_component final : public Audio_out::Session_elem
{
	private:

		Mixer &_mixer;

	public:

		Session_component(Genode::Env &env,
		                  Session_space  &sessions, Session_id id,
		                  Genode::Session_label const &label,
		                  Channel::Number number, Mixer &mixer)
		: Session_elem(env, sessions, mixer.channel_space(number),
		               id, label, mixer.sig_cap()),
		  _mixer(mixer)
		{
			Session_elem::number = number;
			_mixer.add_session(Session_elem::number, *this);
		}

		~Session_component()
		{
			if (Session_rpc_object::active()) stop();
			_mixer.remove_session(Session_elem::number, *this);
		}

		void start()
		{
			Session_rpc_object::start();
			stream()->pos(_mixer.pos(Session_elem::number));
			_mixer.report_channels();
		}

		void stop()
		{
			Session_rpc_object::stop();
			_mixer.report_channels();
		}
};


namespace Audio_out {
	typedef Genode::Root_component<Session_component, Genode::Multiple_clients> Root_component;
}


class Audio_out::Root : public Audio_out::Root_component
{
	private:

		Genode::Env &_env;
		Mixer       &_mixer;
		int          _sessions = { 0 };

		Session_component *_create_session(const char *) { return nullptr; }

		void _destroy_session(Session_component *session)
		{
			if (--_sessions == 0) _mixer.stop();
			Genode::destroy(md_alloc(), session);
		}

	public:

		Root(Genode::Env        &env,
		     Mixer              &mixer,
		     Genode::Allocator  &md_alloc)
		: Root_component(env.ep(), md_alloc), _env(env), _mixer(mixer) { }
};


/***************
 ** Component **
 ***************/

namespace Mixer { struct Main; }

struct Mixer::Main : Genode::Session_requests_handler
{
	Genode::Env &env;

	Genode::Sliced_heap md_alloc { env.ram(), env.rm() };

	Audio_out::Mixer mixer   { env };

	Audio_out::Session_space input_sessions { };

	static Channel::Number channel_from_args(char const *args)
	{
		char channel_name[MAX_CHANNEL_NAME_LEN];
		Genode::Arg_string::find_arg(args, "channel")
			.string(channel_name, sizeof(channel_name), "left");
		return number_from_string(channel_name);
	}

	void process_session_requests() override
	{
		using namespace Genode;

		auto const close_fn = [&] (Parent::Server::Id id) -> bool
		{
			Audio_out::Session_id input_id { id.value };

			bool done = false;
			auto const destroy_input = [&] (Audio_out::Session_component &session)
			{
				PDBG("destroy session for ", session.label);
				env.ep().dissolve(session);
				destroy(md_alloc, &session);
				done = true;
			};

			input_sessions.apply<Audio_out::Session_component&>(input_id, destroy_input);
			if (!done)
				mixer.close_output(env, id);
			return true;
		};

		auto const create_output_fn = [&]
			(Parent::Server::Id id, Session_state::Args const &args)
		{
			Channel::Number channel = channel_from_args(args.string());
			if (channel == Channel::Number::INVALID)
				throw Genode::Service_denied();
			Session_label label = label_from_args(args.string());

			mixer.deliver_output(env, id, label, channel);
		};

		auto const create_input_fn = [&]
			(Parent::Server::Id id, Session_state::Args const &_args)
		{
			using namespace Audio_out;

			char const *args = _args.string();

			/*
			 * We only want to have the last part of the label,
			 * e.g. 'client -> ' => 'client'.
			 */
			Session_label const session_label = label_from_args(args);
			Session_label const label         = session_label.prefix();

			size_t ram_quota =
				Arg_string::find_arg(args, "ram_quota").ulong_value(0);

			size_t session_size = align_addr(sizeof(Session_component), 12);

			if ((ram_quota < session_size) ||
			    (sizeof(Stream) > ram_quota - session_size)) {
				Genode::error("insufficient 'ram_quota', got ", ram_quota, ", "
				              "need ", sizeof(Stream) + session_size);
				throw Insufficient_ram_quota();
			}

			Channel::Number ch = channel_from_args(args);
			if (ch == Channel::Number::INVALID)
				throw Genode::Service_denied();

			Session_component *session = new (md_alloc)
				Session_component(env, input_sessions, Session_id{id.value},
				                  label, ch, mixer);

			//if (++input_sessions == 1) mixer.start();
			PDBG("created session for ", label);
			env.parent().deliver_session_cap(id, env.ep().manage(*session));
		};

		Session_requests_handler::apply_close(close_fn);
		Session_requests_handler::apply_create("Audio_in",  create_output_fn);
		Session_requests_handler::apply_create("Audio_out", create_input_fn);
	}

	Main(Genode::Env &env)
	: Genode::Session_requests_handler(env), env(env)
	{
		env.parent().announce("Audio_out");

		process_session_requests();
	}
};


void Component::construct(Genode::Env &env) { static Mixer::Main inst(env); }
