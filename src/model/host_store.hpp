#pragma once

#include "ssh/ssh_types.hpp"

#include <QObject>
#include <QVector>

namespace arterm::model
{

	/// The saved host list.
	///
	/// Profiles live in a JSON file under the application data directory; secrets
	/// never appear in it and are looked up from `SecretStore` by profile id when a
	/// connection is made.
	class HostStore : public QObject
	{
		Q_OBJECT

	public:
		explicit HostStore( QObject* parent = nullptr );

		/// Default location: ~/Library/Application Support/ArTerm/hosts.json.
		[[nodiscard]] static QString default_file_path();

		[[nodiscard]] QVector<ssh::HostProfile> const& profiles() const noexcept { return _profiles; }
		[[nodiscard]] int count() const noexcept { return static_cast<int>( _profiles.size() ); }

		[[nodiscard]] std::optional<ssh::HostProfile> profile_by_id( QString const& id ) const;

		/// The group names present, in the order they should be shown.
		[[nodiscard]] QStringList groups() const;

		/// Adds a profile, assigning an id when it has none. Returns the id.
		QString add( ssh::HostProfile profile );
		void    update( ssh::HostProfile const& profile );
		void    remove( QString const& id );

		/// Loads secrets from the keychain into a copy of the profile, ready to be
		/// handed to a connection.
		[[nodiscard]] ssh::HostProfile with_secrets( QString const& id ) const;

		/// Persists `profile`'s secrets and strips them from the stored copy.
		void save_secrets( ssh::HostProfile const& profile );

		[[nodiscard]] bool load();
		[[nodiscard]] bool save() const;

		/// Imports hosts from ~/.ssh/config. Returns how many were added.
		int import_from_ssh_config( QString const& config_path = {} );

	Q_SIGNALS:
		void changed();
		void profile_added( QString const& id );
		void profile_updated( QString const& id );
		void profile_removed( QString const& id );

	private:
		[[nodiscard]] int index_of( QString const& id ) const;

		QVector<ssh::HostProfile> _profiles;
		QString                   _file_path;
	};

} // namespace arterm::model
