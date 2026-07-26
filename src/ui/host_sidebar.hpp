#pragma once

#include "ssh/ssh_types.hpp"

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace arterm::model
{
	class HostStore;
}

namespace arterm::ui
{

	/// The host list on the left: search field, grouped entries, and the actions
	/// that create, edit and delete profiles.
	class HostSidebar : public QWidget
	{
		Q_OBJECT

	public:
		HostSidebar( model::HostStore* store, QWidget* parent = nullptr );

		[[nodiscard]] QString selected_host_id() const;

	Q_SIGNALS:
		/// The user activated a host and wants a session opened.
		void connect_requested( QString const& host_id );
		void edit_requested( QString const& host_id );
		void new_host_requested();

	public Q_SLOTS:
		void rebuild();
		void focus_search();

	private:
		void show_context_menu( QPoint const& position );
		void delete_selected();

		model::HostStore* _store;
		QLineEdit*        _search{ nullptr };
		QListWidget*      _list{ nullptr };
		QString           _filter;
	};

} // namespace arterm::ui
