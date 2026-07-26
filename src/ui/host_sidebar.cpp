#include "ui/host_sidebar.hpp"

#include "model/host_store.hpp"
#include "ui/icons.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace arterm::ui
{
	namespace
	{

		constexpr int HOST_ID_ROLE  = Qt::UserRole + 1;
		constexpr int IS_GROUP_ROLE = Qt::UserRole + 2;

	} // namespace

	HostSidebar::HostSidebar( model::HostStore* store, QWidget* parent )
		: QWidget( parent )
		, _store( store )
	{
		setObjectName( QStringLiteral( "sidebar" ) );
		setMinimumWidth( 210 );
		setMaximumWidth( 360 );

		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 0, 0, 0, 0 );
		root->setSpacing( 0 );

		// -- Header ------------------------------------------------------------
		auto* header = new QWidget( this );
		header->setObjectName( QStringLiteral( "sidebarHeader" ) );
		auto* header_layout = new QHBoxLayout( header );
		header_layout->setContentsMargins( 14, 12, 10, 8 );
		header_layout->setSpacing( 6 );

		auto* title = new QLabel( tr( "Hosts" ), header );
		title->setObjectName( QStringLiteral( "sidebarTitle" ) );

		auto* add_button = new QToolButton( header );
		add_button->setIcon( icon( QStringLiteral( "plus" ) ) );
		add_button->setToolTip( tr( "New host (⌘N)" ) );
		add_button->setAutoRaise( true );
		add_button->setIconSize( QSize( 16, 16 ) );

		header_layout->addWidget( title, 1 );
		header_layout->addWidget( add_button );

		// -- Search ------------------------------------------------------------
		auto* search_wrapper = new QWidget( this );
		auto* search_layout  = new QHBoxLayout( search_wrapper );
		search_layout->setContentsMargins( 10, 0, 10, 8 );

		_search = new QLineEdit( search_wrapper );
		_search->setObjectName( QStringLiteral( "searchField" ) );
		_search->setPlaceholderText( tr( "Search hosts" ) );
		_search->setClearButtonEnabled( true );
		_search->addAction( icon( QStringLiteral( "search" ) ), QLineEdit::LeadingPosition );
		search_layout->addWidget( _search );

		// -- List --------------------------------------------------------------
		_list = new QListWidget( this );
		_list->setObjectName( QStringLiteral( "hostList" ) );
		_list->setFrameStyle( QFrame::NoFrame );
		_list->setContextMenuPolicy( Qt::CustomContextMenu );
		_list->setUniformItemSizes( false );
		_list->setSelectionMode( QAbstractItemView::SingleSelection );

		root->addWidget( header );
		root->addWidget( search_wrapper );
		root->addWidget( _list, 1 );

		connect( add_button, &QToolButton::clicked, this, &HostSidebar::new_host_requested );

		connect( _search, &QLineEdit::textChanged, this, [this]( QString const& text ){
			_filter = text.trimmed();
			rebuild();
		} );

		connect( _list, &QListWidget::itemActivated, this, [this]( QListWidgetItem* item ){
			if( item == nullptr || item->data( IS_GROUP_ROLE ).toBool() )
				return;
			Q_EMIT connect_requested( item->data( HOST_ID_ROLE ).toString() );
		} );

		connect( _list, &QListWidget::customContextMenuRequested, this, &HostSidebar::show_context_menu );

		connect( _store, &model::HostStore::changed, this, &HostSidebar::rebuild );

		rebuild();
	}

	QString HostSidebar::selected_host_id() const{
		QListWidgetItem* item = _list->currentItem();
		if( item == nullptr || item->data( IS_GROUP_ROLE ).toBool() )
			return {};
		return item->data( HOST_ID_ROLE ).toString();
	}

	void HostSidebar::focus_search(){
		_search->setFocus( Qt::ShortcutFocusReason );
		_search->selectAll();
	}

	void HostSidebar::rebuild(){
		QString const previous = selected_host_id();

		_list->clear();

		// Bucket the profiles by group so each group can get its own heading.
		QMap<QString, QVector<ssh::HostProfile>> grouped;
		for( ssh::HostProfile const& profile : _store->profiles() ){
			if( !_filter.isEmpty() ){
				bool const matches = profile.display_name().contains( _filter, Qt::CaseInsensitive ) ||
									 profile.hostname.contains( _filter, Qt::CaseInsensitive ) ||
									 profile.username.contains( _filter, Qt::CaseInsensitive ) ||
									 profile.group.contains( _filter, Qt::CaseInsensitive );
				if( !matches )
					continue;
			}

			grouped[profile.group.isEmpty() ? tr( "Hosts" ) : profile.group].append( profile );
		}

		if( grouped.isEmpty() ){
			auto* empty = new QListWidgetItem( _filter.isEmpty() ? tr( "No hosts yet.\nPress ⌘N to add one." )
																 : tr( "Nothing matches “%1”" ).arg( _filter ) );
			empty->setFlags( Qt::NoItemFlags );
			empty->setTextAlignment( Qt::AlignCenter );
			empty->setForeground( QColor( 0x6B, 0x74, 0x82 ) );
			empty->setSizeHint( QSize( 0, 72 ) );
			_list->addItem( empty );
			return;
		}

		for( auto it = grouped.constBegin(); it != grouped.constEnd(); ++it ){
			auto* group_item = new QListWidgetItem( it.key().toUpper() );
			group_item->setData( IS_GROUP_ROLE, true );
			group_item->setFlags( Qt::NoItemFlags );
			group_item->setForeground( QColor( 0x6B, 0x74, 0x82 ) );
			group_item->setSizeHint( QSize( 0, 28 ) );

			QFont group_font = group_item->font();
			group_font.setPointSizeF( group_font.pointSizeF() - 1.5 );
			group_font.setBold( true );
			group_item->setFont( group_font );

			_list->addItem( group_item );

			QVector<ssh::HostProfile> hosts = it.value();
			std::sort( hosts.begin(), hosts.end(), []( ssh::HostProfile const& a, ssh::HostProfile const& b ){
				return a.display_name().compare( b.display_name(), Qt::CaseInsensitive ) < 0;
			} );

			for( ssh::HostProfile const& profile : hosts ){
				auto* item = new QListWidgetItem( profile.display_name() );
				item->setData( HOST_ID_ROLE, profile.id );
				item->setData( IS_GROUP_ROLE, false );
				item->setIcon( icon( QStringLiteral( "server" ) ) );
				item->setToolTip( tr( "%1@%2:%3" ).arg( profile.username, profile.hostname ).arg( profile.port ) );
				item->setSizeHint( QSize( 0, 34 ) );
				_list->addItem( item );

				if( profile.id == previous )
					_list->setCurrentItem( item );
			}
		}
	}

	void HostSidebar::show_context_menu( QPoint const& position ){
		QListWidgetItem* item    = _list->itemAt( position );
		bool const       is_host = item != nullptr && !item->data( IS_GROUP_ROLE ).toBool();

		QMenu menu( this );

		if( is_host ){
			_list->setCurrentItem( item );
			QString const host_id = item->data( HOST_ID_ROLE ).toString();

			menu.addAction( icon( QStringLiteral( "terminal" ) ), tr( "Connect" ), this,
							[this, host_id] { Q_EMIT connect_requested( host_id ); } );
			menu.addAction( icon( QStringLiteral( "edit" ) ), tr( "Edit…" ), this,
							[this, host_id] { Q_EMIT edit_requested( host_id ); } );
			menu.addSeparator();
			menu.addAction( icon( QStringLiteral( "trash" ) ), tr( "Delete…" ), this, &HostSidebar::delete_selected );
			menu.addSeparator();
		}

		menu.addAction( icon( QStringLiteral( "plus" ) ), tr( "New Host…" ), this, &HostSidebar::new_host_requested );

		menu.exec( _list->viewport()->mapToGlobal( position ) );
	}

	void HostSidebar::delete_selected(){
		QString const host_id = selected_host_id();
		if( host_id.isEmpty() )
			return;

		auto const profile = _store->profile_by_id( host_id );
		if( !profile )
			return;

		auto const answer = QMessageBox::warning(
			this, tr( "Delete Host" ),
			tr( "Delete “%1”?\nSaved credentials for this host are removed from the keychain as well." )
				.arg( profile->display_name() ),
			QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel );

		if( answer == QMessageBox::Yes )
			_store->remove( host_id );
	}

} // namespace arterm::ui
