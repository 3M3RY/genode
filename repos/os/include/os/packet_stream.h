/*
 * \brief  Packet-streaming interface
 * \author Norman Feske
 * \date   2009-11-10
 *
 * This interface support the unidirectional streaming of bulk data between
 * processes via a shared-memory block. The public interface consists of
 * the two class templates 'Packet_stream_source', and 'Packet_stream_sink'.
 * Both communication parties agree on a policy with regard to the
 * organization of the communication buffer by specifying the same
 * 'Packet_stream_policy' as template argument.
 *
 * The communication buffer consists of three parts, a submit queue, an
 * acknowledgement queue, and a bulk buffer. The submit queue contains
 * packets generated by the source to be processed by the sink. The
 * acknowledgement queue contains packets that are processed and acknowledged
 * by the sink. The bulk buffer contains the actual payload. The assignment
 * of packets to bulk-buffer regions is performed by the source.
 *
 * The interplay between source and sink for processing a single packet looks
 * as follows:
 * # The source allocates a region of the bulk buffer for storing the packet
 *   payload using 'alloc_packet'
 * # The source requests the local start address of the payload using
 *   'packet_content' and fills the packet with data
 * # The source submits the packet to the submit queue via 'submit_packet'
 * # The sink requests a packet from the submit queue using 'get_packet'
 * # The sink requests the local start address of the payload using
 *   'packet_content' and processes the contained data
 * # After having finished the processing of the packet, the sink acknowledges
 *   the packet using 'acknowledge_packet', placing the packet into the
 *   acknowledgement queue
 * # The source reads the packet from the acknowledgement queue and releases
 *   the packet using 'release_packet'. Thereby, the region of the bulk buffer
 *   that was used by the packet becomes marked as free.
 *
 * This protocol has four corner cases that are handled by signals:
 *
 * :submit queue is full: when the source is trying to submit a new packet.
 *   In this case, the source blocks and waits for the sink to remove packets
 *   from the submit queue. If the sink observes such a condition (calling
 *   'get_packet' on a full submit queue, it delivers a 'ready_to_submit'
 *   signal to wake up the source.
 *
 * :submit queue is empty: when the sink tries to obtain a packet via
 *   'get_packet'. The sink is going to block. If the source places a
 *   packet into an empty submit queue, it delivers a 'packet_avail'
 *   signal to wake up the sink.
 *
 * :acknowledgement queue is full: when the sink tries to acknowledge a packet
 *   using 'acknowledge_packet'. The sink is going to block until the source
 *   removes an acknowledged packet from the acknowledgement queue and delivers
 *   a 'ready_to_ack' signal.
 *
 * :acknowledgement queue is empty: when the source tries to obtain an
 *   acknowledged packet using 'get_acked_packet'. In this case, the source
 *   will block until the sink places another acknowledged packet into the
 *   empty acknowledgement queue and delivers a 'ack_avail' signal.
 *
 * These conditions can be avoided by querying the state of the submit and
 * acknowledge buffers using the methods 'packet_avail',
 * 'ready_to_submit', 'ready_to_ack', and 'ack_avail'.
 *
 * If bidirectional data exchange between two processes is desired, two pairs
 * of 'Packet_stream_source' and 'Packet_stream_sink' should be instantiated.
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__OS__PACKET_STREAM_H_
#define _INCLUDE__OS__PACKET_STREAM_H_

/* Genode includes */
#include <base/env.h>
#include <base/signal.h>
#include <base/allocator.h>
#include <dataspace/client.h>
#include <util/string.h>
#include <util/construct_at.h>

namespace Genode {

	class Packet_descriptor;

	template <typename, int> class Packet_descriptor_queue;
	template <typename>      class Packet_descriptor_transmitter;
	template <typename>      class Packet_descriptor_receiver;

	class Packet_stream_base;

	template <typename, unsigned, unsigned, typename>
	struct Packet_stream_policy;

	/**
	 * Default configuration for packet-descriptor queues
	 */
	typedef Packet_stream_policy<Packet_descriptor, 64, 64, char>
	        Default_packet_stream_policy;

	template <typename POLICY = Default_packet_stream_policy>
	class Packet_stream_source;

	template <typename POLICY = Default_packet_stream_policy>
	class Packet_stream_sink;
}


/**
 * Default packet descriptor
 *
 * A class used as 'PACKET_DESCRIPTOR' arguments to the 'Packet_stream_policy'
 * template must implement the interface of this class.
 */
class Genode::Packet_descriptor
{
	private:

		Genode::off_t  _offset;
		Genode::size_t _size;

	public:

		/**
		 * Default packet alignment used in allocation
		 */
		enum Alignment { PACKET_ALIGNMENT = 0 };

		/**
		 * Exception type thrown by packet streams
		 */
		class Invalid_packet { };

		/**
		 * Constructor
		 */
		Packet_descriptor(Genode::off_t offset, Genode::size_t size) :
			_offset(offset), _size(size) { }

		/**
		 * Default constructor used for instantiating arrays of
		 * packet-descriptors used as submit and ack queues.
		 */
		Packet_descriptor() : _offset(0), _size(0) { }

		Genode::off_t  offset() const { return _offset; }
		Genode::size_t size()   const { return _size; }
};


/**
 * Ring buffer shared between source and sink, containing packet descriptors
 *
 * This class is private to the packet-stream interface.
 */
template <typename PACKET_DESCRIPTOR, int QUEUE_SIZE>
class Genode::Packet_descriptor_queue
{
	private:

		/*
		 * The anonymous struct is needed to skip the initialization of the
		 * members, which are shared by both sides of the packet stream.
		 */
		struct
		{
			unsigned volatile _head;
			unsigned volatile _tail;
			PACKET_DESCRIPTOR _queue[QUEUE_SIZE];
		};

	public:

		typedef PACKET_DESCRIPTOR Packet_descriptor;

		enum Role { PRODUCER, CONSUMER };

		/**
		 * Constructor
		 *
		 * Because the 'Packet_descriptor_queue' is constructed twice (at the
		 * source and at the sink) inside a shared-memory block, the
		 * constructor must know the role of the instance to initialize only
		 * those members that are driven by the respective role.
		 */
		Packet_descriptor_queue(Role role)
		{
			if (role == PRODUCER) {
				_head = 0;
				Genode::memset(_queue, 0, sizeof(_queue));
			} else
				_tail = 0;
		}

		/**
		 * Place packet descriptor into queue
		 *
		 * \return true on success, or
		 *         false if queue is full
		 */
		bool add(PACKET_DESCRIPTOR packet)
		{
			if (full()) return false;

			_queue[_head%QUEUE_SIZE] = packet;
			_head = (_head + 1)%QUEUE_SIZE;
			return true;
		}

		/**
		 * Take packet descriptor from queue
		 *
		 * \return  packet descriptor
		 */
		PACKET_DESCRIPTOR get()
		{
			PACKET_DESCRIPTOR packet = _queue[_tail%QUEUE_SIZE];
			_tail = (_tail + 1)%QUEUE_SIZE;
			return packet;
		}

		/**
		 * Return current packet descriptor
		 */
		PACKET_DESCRIPTOR peek() const
		{
			return _queue[_tail%QUEUE_SIZE];
		}

		/**
		 * Return true if packet-descriptor queue is empty
		 */
		bool empty() { return _tail == _head; }

		/**
		 * Return true if packet-descriptor queue is full
		 */
		bool full() { return (_head + 1)%QUEUE_SIZE == _tail; }

		/**
		 * Return true if a single element is stored in the queue
		 */
		bool single_element() { return (_tail + 1)%QUEUE_SIZE == _head; }


		/**
		 * Return true if a single slot is left to be put into the queue
		 */
		bool single_slot_free() { return (_head + 2)%QUEUE_SIZE == _tail; }

		/**
		 * Return number of slots left to be put into the queue
		 */
		unsigned slots_free() {
			return ((_tail > _head) ? _tail - _head
			                        : QUEUE_SIZE - _head + _tail) - 1; }
};


/**
 * Transmit packet descriptors with data-flow control
 *
 * This class is private to the packet-stream interface.
 */
template <typename TX_QUEUE>
class Genode::Packet_descriptor_transmitter
{
	private:

		/* facility to receive ready-to-transmit signals */
		Genode::Signal_receiver           _tx_ready         { };
		Genode::Signal_context            _tx_ready_context { };
		Genode::Signal_context_capability _tx_ready_cap;

		/* facility to send ready-to-receive signals */
		Genode::Signal_transmitter         _rx_ready { };

		Genode::Lock _tx_queue_lock { };
		TX_QUEUE    *_tx_queue;
		bool         _tx_wakeup_needed = false;

		/*
		 * Noncopyable
		 */
		Packet_descriptor_transmitter(Packet_descriptor_transmitter const &);
		Packet_descriptor_transmitter &operator = (Packet_descriptor_transmitter const &);

	public:

		/**
		 * Constructor
		 */
		Packet_descriptor_transmitter(TX_QUEUE *tx_queue)
		:
			_tx_ready_cap(_tx_ready.manage(&_tx_ready_context)),
			_tx_queue(tx_queue)
		{ }

		~Packet_descriptor_transmitter()
		{
			_tx_ready.dissolve(&_tx_ready_context);
		}

		Genode::Signal_context_capability tx_ready_cap()
		{
			return _tx_ready_cap;
		}

		void register_rx_ready_cap(Genode::Signal_context_capability cap)
		{
			_rx_ready.context(cap);

			/*
			 * if a packet was already put into the queue
			 * before a signal handler was registered,
			 * a signal has to be send again
			 */
			if (!_tx_queue->empty())
				_rx_ready.submit();
		}

		bool ready_for_tx()
		{
			Genode::Lock::Guard lock_guard(_tx_queue_lock);
			return !_tx_queue->full();
		}

		void tx(typename TX_QUEUE::Packet_descriptor packet)
		{
			Genode::Lock::Guard lock_guard(_tx_queue_lock);

			do {
				/* block for signal if tx queue is full */
				if (_tx_queue->full())
					_tx_ready.wait_for_signal();

				/*
				 * It could happen that pending signals do not refer to the
				 * current queue situation. Therefore, we need to double check
				 * if the queue insertion succeeds and retry if needed.
				 */

			} while (_tx_queue->add(packet) == false);

			if (_tx_queue->single_element())
				_rx_ready.submit();
		}

		bool try_tx(typename TX_QUEUE::Packet_descriptor packet)
		{
			Genode::Lock::Guard lock_guard(_tx_queue_lock);

			if (_tx_queue->full())
				return false;

			_tx_queue->add(packet);

			if (_tx_queue->single_element())
				_tx_wakeup_needed = true;

			return true;
		}

		bool tx_wakeup()
		{
			Genode::Lock::Guard lock_guard(_tx_queue_lock);

			bool signal_submitted = false;

			if (_tx_wakeup_needed) {
				_rx_ready.submit();
				signal_submitted = true;
			}

			_tx_wakeup_needed = false;
			return signal_submitted;
		}

		/**
		 * Return number of slots left to be put into the tx queue
		 */
		unsigned tx_slots_free() { return _tx_queue->slots_free(); }
};


/**
 * Receive packet descriptors with data-flow control
 *
 * This class is private to the packet-stream interface.
 */
template <typename RX_QUEUE>
class Genode::Packet_descriptor_receiver
{
	private:

		/* facility to receive ready-to-receive signals */
		Genode::Signal_receiver           _rx_ready         { };
		Genode::Signal_context            _rx_ready_context { };
		Genode::Signal_context_capability _rx_ready_cap;

		/* facility to send ready-to-transmit signals */
		Genode::Signal_transmitter        _tx_ready { };

		Genode::Lock mutable  _rx_queue_lock { };
		RX_QUEUE             *_rx_queue;
		bool                  _rx_wakeup_needed = false;

		/*
		 * Noncopyable
		 */
		Packet_descriptor_receiver(Packet_descriptor_receiver const &);
		Packet_descriptor_receiver &operator = (Packet_descriptor_receiver const &);

	public:

		/**
		 * Constructor
		 */
		Packet_descriptor_receiver(RX_QUEUE *rx_queue)
		:
			_rx_ready_cap(_rx_ready.manage(&_rx_ready_context)),
			_rx_queue(rx_queue)
		{ }

		~Packet_descriptor_receiver()
		{
			_rx_ready.dissolve(&_rx_ready_context);
		}

		Genode::Signal_context_capability rx_ready_cap()
		{
			return _rx_ready_cap;
		}

		void register_tx_ready_cap(Genode::Signal_context_capability cap)
		{
			_tx_ready.context(cap);

			/*
			 * if a packet was already put into the queue
			 * before a signal handler was registered,
			 * a signal has to be send again
			 */
			if (!_rx_queue->empty())
				_tx_ready.submit();
		}

		bool ready_for_rx()
		{
			Genode::Lock::Guard lock_guard(_rx_queue_lock);
			return !_rx_queue->empty();
		}

		void rx(typename RX_QUEUE::Packet_descriptor *out_packet)
		{
			Genode::Lock::Guard lock_guard(_rx_queue_lock);

			while (_rx_queue->empty())
				_rx_ready.wait_for_signal();

			*out_packet = _rx_queue->get();

			if (_rx_queue->single_slot_free())
				_tx_ready.submit();
		}

		typename RX_QUEUE::Packet_descriptor try_rx()
		{
			Genode::Lock::Guard lock_guard(_rx_queue_lock);

			typename RX_QUEUE::Packet_descriptor packet { };

			if (!_rx_queue->empty())
				packet = _rx_queue->get();

			if (_rx_queue->single_slot_free())
				_rx_wakeup_needed = true;

			return packet;
		}

		bool rx_wakeup()
		{
			Genode::Lock::Guard lock_guard(_rx_queue_lock);

			bool signal_submitted = false;

			if (_rx_wakeup_needed) {
				_tx_ready.submit();
				signal_submitted = true;
			}

			_rx_wakeup_needed = false;

			return signal_submitted;
		}

		typename RX_QUEUE::Packet_descriptor rx_peek() const
		{
			Genode::Lock::Guard lock_guard(_rx_queue_lock);
			return _rx_queue->peek();
		}
};


/**
 * Common base of 'Packet_stream_source' and 'Packet_stream_sink'
 */
class Genode::Packet_stream_base
{
	public:

		/**
		 * Exception type
		 */
		class Transport_dataspace_too_small { };

	private:

		/*
		 * Noncopyable
		 */
		Packet_stream_base(Packet_stream_base const &);
		Packet_stream_base &operator = (Packet_stream_base const &);

	protected:

		Genode::Region_map          &_rm;
		Genode::Dataspace_capability _ds_cap;
		void                        *_ds_local_base;
		Genode::size_t               _ds_size { 0 };

		Genode::off_t  _submit_queue_offset;
		Genode::off_t  _ack_queue_offset;
		Genode::off_t  _bulk_buffer_offset;
		Genode::size_t _bulk_buffer_size { 0 };

		/**
		 * Constructor
		 *
		 * \param submit_queue_size  submit queue size in bytes
		 * \param ack_queue_size     acknowledgement queue size in bytes
		 * \throw                    'Transport_dataspace_too_small'
		 */
		Packet_stream_base(Genode::Dataspace_capability transport_ds,
		                   Genode::Region_map &rm,
		                   Genode::size_t submit_queue_size,
		                   Genode::size_t ack_queue_size)
		:
			_rm(rm), _ds_cap(transport_ds),

			/* map dataspace locally */
			_ds_local_base(rm.attach(_ds_cap)),
			_submit_queue_offset(0),
			_ack_queue_offset(_submit_queue_offset + submit_queue_size),
			_bulk_buffer_offset(align_addr(_ack_queue_offset + ack_queue_size, 6))
		{
			Genode::size_t ds_size = Genode::Dataspace_client(_ds_cap).size();

			if ((Genode::size_t)_bulk_buffer_offset >= ds_size)
				throw Transport_dataspace_too_small();

			_ds_size = ds_size;
			_bulk_buffer_size = ds_size - _bulk_buffer_offset;
		}

		/**
		 * Destructor
		 */
		~Packet_stream_base()
		{
			/*
			 * Prevent throwing exceptions from the destructor. Otherwise,
			 * the compiler may generate implicit calls to 'std::terminate'.
			 */
			try {
				/* unmap transport dataspace locally */
				_rm.detach(_ds_local_base);
			} catch (...) { }
		}

		void *_submit_queue_local_base() {
			return (void *)((Genode::addr_t)_ds_local_base + _submit_queue_offset); }

		void *_ack_queue_local_base() {
			return (void *)((Genode::addr_t)_ds_local_base + _ack_queue_offset); }

		Genode::addr_t _bulk_buffer_local_base() {
			return (Genode::addr_t)_ds_local_base + _bulk_buffer_offset; }

		/**
		 * Hook for unit testing
		 */
		void _debug_print_buffers();

		/**
		 * Return communication buffer
		 */
		Genode::Dataspace_capability _dataspace() { return _ds_cap; }

		bool packet_valid(Packet_descriptor packet)
		{
			return !packet.size() || (packet.offset() >= _bulk_buffer_offset
				 && packet.offset() < _bulk_buffer_offset + (Genode::off_t)_bulk_buffer_size
				 && packet.offset() + packet.size() <= _bulk_buffer_offset + _bulk_buffer_size);
		}

		template<typename CONTENT_TYPE>
		CONTENT_TYPE *packet_content(Packet_descriptor packet)
		{
			if (!packet.size()) return nullptr;

			if (!packet_valid(packet) || packet.size() < sizeof(CONTENT_TYPE))
				throw Packet_descriptor::Invalid_packet();

			return (CONTENT_TYPE *)((Genode::addr_t)_ds_local_base + packet.offset());
		}

		Genode::addr_t ds_local_base() const { return (Genode::addr_t)_ds_local_base; }
		Genode::addr_t ds_size()       const { return _ds_size; }
};


/**
 * Policy used by both sides source and sink
 */
template <typename PACKET_DESCRIPTOR,
          unsigned SUBMIT_QUEUE_SIZE,
          unsigned ACK_QUEUE_SIZE,
          typename CONTENT_TYPE>
struct Genode::Packet_stream_policy
{
	typedef CONTENT_TYPE Content_type;

	typedef PACKET_DESCRIPTOR Packet_descriptor;

	typedef Packet_descriptor_queue<PACKET_DESCRIPTOR, SUBMIT_QUEUE_SIZE>
	        Submit_queue;

	typedef Packet_descriptor_queue<PACKET_DESCRIPTOR, ACK_QUEUE_SIZE>
	        Ack_queue;
};


/**
 * Originator of a packet stream
 */
template <typename POLICY>
class Genode::Packet_stream_source : private Packet_stream_base
{
	public:

		typedef typename POLICY::Packet_descriptor Packet_descriptor;

	private:

		typedef typename POLICY::Submit_queue Submit_queue;
		typedef typename POLICY::Ack_queue    Ack_queue;
		typedef typename POLICY::Content_type Content_type;

		Genode::Range_allocator &_packet_alloc;

		Packet_descriptor_transmitter<Submit_queue> _submit_transmitter;
		Packet_descriptor_receiver<Ack_queue>       _ack_receiver;

	public:

		/**
		 * Exception type
		 */
		class Packet_alloc_failed { };

		/**
		 * Constructor
		 *
		 * \param transport_ds  dataspace used for communication buffer shared
		 *                      between source and sink
		 * \param rm            region to map buffer dataspace into
		 * \param packet_alloc  allocator for managing packet allocation within
		 *                      the shared communication buffer
		 *
		 * The 'packet_alloc' must not be pre-initialized. It will be
		 * initialized by the constructor using dataspace-relative offsets
		 * rather than pointers.
		 */
		Packet_stream_source(Genode::Dataspace_capability  transport_ds_cap,
		                     Genode::Region_map           &rm,
		                     Genode::Range_allocator      &packet_alloc)
		:
			Packet_stream_base(transport_ds_cap, rm,
			                   sizeof(Submit_queue),
			                   sizeof(Ack_queue)),
			_packet_alloc(packet_alloc),

			/* construct packet-descriptor queues */
			_submit_transmitter(construct_at<Submit_queue>(_submit_queue_local_base(),
			                                               Submit_queue::PRODUCER)),
			_ack_receiver(construct_at<Ack_queue>(_ack_queue_local_base(),
			                                      Ack_queue::CONSUMER))
		{
			/* initialize packet allocator */
			_packet_alloc.add_range(_bulk_buffer_offset,
			                         _bulk_buffer_size);
		}

		~Packet_stream_source()
		{
			_packet_alloc.remove_range(_bulk_buffer_offset,
			                            _bulk_buffer_size);
		}

		using Packet_stream_base::packet_valid;

		/**
		 * Return the size of the bulk buffer.
		 */
		Genode::size_t bulk_buffer_size() { return _bulk_buffer_size; }

		/**
		 * Register signal handler for receiving the signal that new packets
		 * are available in the submit queue.
		 */
		void register_sigh_packet_avail(Genode::Signal_context_capability cap)
		{
			_submit_transmitter.register_rx_ready_cap(cap);
		}

		/**
		 * Register signal handler for receiving the signal that there is new
		 * space for new acknowledgements in the ack queue.
		 */
		void register_sigh_ready_to_ack(Genode::Signal_context_capability cap)
		{
			_ack_receiver.register_tx_ready_cap(cap);
		}

		/**
		 * Return signal handler for handling signals indicating that new
		 * packets can be submitted.
		 */
		Genode::Signal_context_capability sigh_ready_to_submit()
		{
			return _submit_transmitter.tx_ready_cap();
		}

		/**
		 * Return signal handler for handling signals indicating that
		 * new acknowledgements are available.
		 */
		Genode::Signal_context_capability sigh_ack_avail()
		{
			return _ack_receiver.rx_ready_cap();
		}

		/**
		 * Allocate packet
		 *
		 * \param size   size of packet in bytes
		 * \param align  alignment of packet as log2 value, default is 1 byte
		 * \throws       'Packet_alloc_failed'
		 * \return       packet descriptor with an assigned range within the
		 *               bulk buffer shared between source and sink
		 */
		Packet_descriptor alloc_packet(Genode::size_t size, int align = POLICY::Packet_descriptor::PACKET_ALIGNMENT)
		{
			void *base = 0;
			if (size && _packet_alloc.alloc_aligned(size, &base, align).error())
				throw Packet_alloc_failed();

			return Packet_descriptor((Genode::off_t)base, size);
		}

		/**
		 * Get pointer to the content of the specified packet
		 *
		 * \throw Packet_descriptor::Invalid_packet  raise an exception if
		                                             the packet is invalid
		 */
		Content_type *packet_content(Packet_descriptor packet) {
			return Packet_stream_base::packet_content<Content_type>(packet); }

		/**
		 * Return true if submit queue can hold 'count' additional packets
		 */
		bool ready_to_submit(unsigned count = 1)
		{
			return _submit_transmitter.tx_slots_free() >= count;
		}

		/**
		 * Tell sink about a packet to process
		 */
		void submit_packet(Packet_descriptor packet)
		{
			_submit_transmitter.tx(packet);
		}

		/**
		 * Submit the specified packet to the server if possible
		 *
		 * \return false if the submit queue is congested
		 *
		 * This method never blocks.
		 */
		bool try_submit_packet(Packet_descriptor packet)
		{
			return _submit_transmitter.try_tx(packet);
		}

		/**
		 * Wake up the packet sink if needed
		 *
		 * This method assumes that the same signal handler is used for
		 * the submit transmitter and the ack receiver.
		 */
		void wakeup()
		{
			/* submit only one signal */
			_submit_transmitter.tx_wakeup() || _ack_receiver.rx_wakeup();
		}

		/**
		 * Returns true if one or more packet acknowledgements are available
		 */
		bool ack_avail() { return _ack_receiver.ready_for_rx(); }

		/**
		 * Get acknowledged packet
		 */
		Packet_descriptor get_acked_packet()
		{
			Packet_descriptor packet;
			_ack_receiver.rx(&packet);
			return packet;
		}

		/**
		 * Return next acknowledgement from sink, or an invalid packet
		 *
		 * This method never blocks.
		 */
		Packet_descriptor try_get_acked_packet()
		{
			return _ack_receiver.try_rx();
		}

		/**
		 * Release bulk-buffer space consumed by the packet
		 */
		void release_packet(Packet_descriptor packet)
		{
			if (packet.size())
				_packet_alloc.free((void *)packet.offset(), packet.size());
		}

		void debug_print_buffers() {
			Packet_stream_base::_debug_print_buffers(); }

		Genode::Dataspace_capability dataspace() {
			return Packet_stream_base::_dataspace(); }

		Genode::addr_t ds_local_base() const { return reinterpret_cast<Genode::addr_t>(_ds_local_base); }
		Genode::addr_t ds_size()       const { return Packet_stream_base::_ds_size; }
};


/**
 * Receiver of a packet stream
 */
template <typename POLICY>
class Genode::Packet_stream_sink : private Packet_stream_base
{
	public:

		typedef typename POLICY::Submit_queue      Submit_queue;
		typedef typename POLICY::Ack_queue         Ack_queue;
		typedef typename POLICY::Packet_descriptor Packet_descriptor;
		typedef typename POLICY::Content_type      Content_type;

	private:

		Packet_descriptor_receiver<Submit_queue> _submit_receiver;
		Packet_descriptor_transmitter<Ack_queue> _ack_transmitter;

	public:

		/**
		 * Constructor
		 *
		 * \param transport_ds  dataspace used for communication buffer shared between
		 *                      source and sink
		 */
		Packet_stream_sink(Genode::Dataspace_capability transport_ds,
		                   Genode::Region_map &rm)
		:
			Packet_stream_base(transport_ds, rm, sizeof(Submit_queue), sizeof(Ack_queue)),

			/* construct packet-descriptor queues */
			_submit_receiver(construct_at<Submit_queue>(_submit_queue_local_base(),
			                                            Submit_queue::CONSUMER)),
			_ack_transmitter(construct_at<Ack_queue>(_ack_queue_local_base(),
			                                         Ack_queue::PRODUCER))
		{ }

		using Packet_stream_base::packet_valid;

		/**
		 * Register signal handler to notify that new acknowledgements
		 * are available in the ack queue.
		 */
		void register_sigh_ack_avail(Genode::Signal_context_capability cap)
		{
			_ack_transmitter.register_rx_ready_cap(cap);
		}

		/**
		 * Register signal handler to notify that new packets
		 * can be submitted into the submit queue.
		 */
		void register_sigh_ready_to_submit(Genode::Signal_context_capability cap)
		{
			_submit_receiver.register_tx_ready_cap(cap);
		}

		/**
		 * Return signal handler for handling signals indicating that
		 * new acknowledgements can be generated.
		 */
		Genode::Signal_context_capability sigh_ready_to_ack()
		{
			return _ack_transmitter.tx_ready_cap();
		}

		/**
		 * Return signal handler for handling signals indicating that
		 * new packets are available in the submit queue.
		 */
		Genode::Signal_context_capability sigh_packet_avail()
		{
			return _submit_receiver.rx_ready_cap();
		}

		/**
		 * Return true if a packet is available
		 */
		bool packet_avail() { return _submit_receiver.ready_for_rx(); }

		/**
		 * Get next packet from source
		 *
		 * This method blocks if no packets are available.
		 */
		Packet_descriptor get_packet()
		{
			Packet_descriptor packet;
			_submit_receiver.rx(&packet);
			return packet;
		}

		/**
		 * Return next packet from source, or an invalid packet
		 *
		 * This method never blocks.
		 */
		Packet_descriptor try_get_packet()
		{
			return _submit_receiver.try_rx();
		}

		/**
		 * Wake up the packet source if needed
		 *
		 * This method assumes that the same signal handler is used for
		 * the submit receiver and the ack transmitter.
		 */
		void wakeup()
		{
			/* submit only one signal */
			_submit_receiver.rx_wakeup() || _ack_transmitter.tx_wakeup();
		}

		/**
		 * Return but do not dequeue next packet
		 *
		 * If there is no packet, an invalid packet descriptor is returned.
		 */
		Packet_descriptor peek_packet() const
		{
			return _submit_receiver.rx_peek();
		}

		/**
		 * Get pointer to the content of the specified packet
		 *
		 * \throw Packet_descriptor::Invalid_packet  raise an exception if
		                                             the packet is invalid
		 */
		Content_type *packet_content(Packet_descriptor packet) {
			return Packet_stream_base::packet_content<Content_type>(packet); }

		/**
		 * Returns true if no further acknowledgements can be submitted
		 */
		bool ready_to_ack() { return _ack_transmitter.ready_for_tx(); }

		/**
		 * Returns number of slots left in the the ack queue
		 */
		unsigned ack_slots_free() {
			return _ack_transmitter.tx_slots_free(); }

		/**
		 * Tell the source that the processing of the specified packet is completed
		 *
		 * This method blocks if the acknowledgement queue is full.
		 */
		void acknowledge_packet(Packet_descriptor packet)
		{
			_ack_transmitter.tx(packet);
		}

		/**
		 * Acknowledge the specified packet to the client if possible
		 *
		 * \return false if the acknowledgement queue is congested
		 *
		 * This method never blocks.
		 */
		bool try_ack_packet(Packet_descriptor packet)
		{
			return _ack_transmitter.try_tx(packet);
		}

		void debug_print_buffers() {
			Packet_stream_base::_debug_print_buffers(); }

		Genode::Dataspace_capability dataspace() {
			return Packet_stream_base::_dataspace(); }

		Genode::addr_t ds_local_base() const { return reinterpret_cast<Genode::addr_t>(_ds_local_base); }
		Genode::addr_t ds_size()       const { return Packet_stream_base::_ds_size; }
};

#endif /* _INCLUDE__OS__PACKET_STREAM_H_ */

