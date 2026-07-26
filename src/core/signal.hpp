#pragma once

#include "core/non_copyable.hpp"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace arterm
{

	using SlotId = std::uint64_t;

	/// A one-to-many callback list: the replacement for Qt's signals.
	///
	/// Not thread-safe by design. A signal is emitted on whatever queue its owner
	/// runs on, and a slot that needs the main queue marshals there itself with
	/// `on_main` - that keeps the threading explicit instead of hiding it behind a
	/// connection type.
	///
	/// Disconnecting from inside a slot is safe: the entry is only tombstoned
	/// during emission and compacted once the outermost emission unwinds.
	template <typename... Args_>
	class Signal : NonCopyable
	{
	public:
		using Slot = std::function<void( Args_... )>;

		SlotId connect( Slot slot ){
			SlotId const id = _next_id++;
			_slots.push_back( Entry{ id, std::move( slot ) } );
			return id;
		}

		void disconnect( SlotId id ){
			for( auto it = _slots.begin(); it != _slots.end(); ++it ){
				if( it->id != id )
					continue;

				if( _depth > 0 ){
					it->slot = nullptr;
				}
				else{
					_slots.erase( it );
				}
				return;
			}
		}

		void disconnect_all(){
			if( _depth > 0 ){
				for( Entry& entry : _slots )
					entry.slot = nullptr;
			}
			else{
				_slots.clear();
			}
		}

		[[nodiscard]] bool has_slots() const noexcept { return !_slots.empty(); }

		/// Slots connected during an emission do not fire until the next one.
		void operator()( Args_... args ) const{
			std::size_t const count = _slots.size();
			++_depth;
			for( std::size_t i = 0; i < count && i < _slots.size(); ++i ){
				if( _slots[i].slot )
					_slots[i].slot( args... );
			}
			--_depth;

			if( _depth == 0 )
				compact();
		}

	private:
		struct Entry
		{
			SlotId id;
			Slot   slot;
		};

		void compact() const{
			std::erase_if( _slots, []( Entry const& entry ) { return !entry.slot; } );
		}

		mutable std::vector<Entry> _slots;
		mutable unsigned           _depth{ 0 };
		SlotId                     _next_id{ 1 };
	};

	/// Unsubscribes on destruction, for the cases where a slot outlives its owner
	/// unless something intervenes.
	template <typename Signal_>
	class ScopedConnection : NonCopyable
	{
	public:
		ScopedConnection() = default;

		ScopedConnection( Signal_& signal, SlotId id ) noexcept
			: _signal( &signal )
			, _id( id )
		{}

		ScopedConnection( ScopedConnection&& other ) noexcept
			: _signal( std::exchange( other._signal, nullptr ) )
			, _id( std::exchange( other._id, 0 ) )
		{}

		ScopedConnection& operator=( ScopedConnection&& other ) noexcept{
			if( this != &other ){
				release();
				_signal = std::exchange( other._signal, nullptr );
				_id     = std::exchange( other._id, 0 );
			}
			return *this;
		}

		~ScopedConnection() { release(); }

		void release() noexcept{
			if( _signal != nullptr && _id != 0 )
				_signal->disconnect( _id );
			_signal = nullptr;
			_id     = 0;
		}

	private:
		Signal_* _signal{ nullptr };
		SlotId   _id{ 0 };
	};

} // namespace arterm
