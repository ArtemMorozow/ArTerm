#include "files/file_pane.hpp"

#include "files/file_list_model.hpp"
#include "ui/icons.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSortFilterProxyModel>
#include <QStackedLayout>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY( lc_files, "arterm.files" )

namespace arterm::files
{
	namespace
	{

		/// Sorts by the raw value behind each column rather than by the formatted
		/// string, so "9 bytes" does not end up after "10 KB".
		class FileSortProxy : public QSortFilterProxyModel
		{
		public:
			using QSortFilterProxyModel::QSortFilterProxyModel;

		protected:
			bool lessThan( QModelIndex const& left, QModelIndex const& right ) const override{
				bool const left_is_dir  = left.data( IS_DIRECTORY_ROLE ).toBool();
				bool const right_is_dir = right.data( IS_DIRECTORY_ROLE ).toBool();

				// Folders always lead, regardless of the sort column or order. The
				// order is inverted for descending sorts so they do not sink instead.
				if( left_is_dir != right_is_dir )
					return sortOrder() == Qt::AscendingOrder ? left_is_dir : !left_is_dir;

				switch( static_cast<FileColumn>( left.column() ) ){
					case FileColumn::SIZE:
						return left.data( SIZE_ROLE ).toULongLong() < right.data( SIZE_ROLE ).toULongLong();
					case FileColumn::MODIFIED:
						return left.data( MODIFIED_ROLE ).toDateTime() < right.data( MODIFIED_ROLE ).toDateTime();
					default:
						return left.data( NAME_ROLE )
								   .toString()
								   .compare( right.data( NAME_ROLE ).toString(), Qt::CaseInsensitive ) < 0;
				}
			}

			bool filterAcceptsRow( int row, QModelIndex const& parent ) const override{
				if( filterRegularExpression().pattern().isEmpty() )
					return true;
				QModelIndex const index = sourceModel()->index( row, 0, parent );
				return index.data( NAME_ROLE ).toString().contains( filterRegularExpression() );
			}
		};

	} // namespace

	// ---------------------------------------------------------------------------
	// FileTableView
	// ---------------------------------------------------------------------------

	FileTableView::FileTableView( QWidget* parent )
		: QWidget( parent )
		, _view( new QTableView( this ) )
		, _proxy( new FileSortProxy( this ) )
	{
		auto* layout = new QVBoxLayout( this );
		layout->setContentsMargins( 0, 0, 0, 0 );
		layout->addWidget( _view );

		_view->setObjectName( QStringLiteral( "fileTable" ) );
		_view->setSelectionBehavior( QAbstractItemView::SelectRows );
		_view->setSelectionMode( QAbstractItemView::ExtendedSelection );
		_view->setSortingEnabled( true );
		_view->setAlternatingRowColors( false );
		_view->setShowGrid( false );
		_view->setWordWrap( false );
		_view->setFrameStyle( QFrame::NoFrame );
		_view->setContextMenuPolicy( Qt::CustomContextMenu );
		_view->setEditTriggers( QAbstractItemView::NoEditTriggers );

		_view->setDragEnabled( true );
		_view->setAcceptDrops( true );
		_view->setDropIndicatorShown( true );
		_view->setDragDropMode( QAbstractItemView::DragDrop );
		_view->setDefaultDropAction( Qt::CopyAction );

		_view->verticalHeader()->setVisible( false );
		_view->verticalHeader()->setDefaultSectionSize( 26 );
		_view->horizontalHeader()->setHighlightSections( false );
		_view->horizontalHeader()->setStretchLastSection( false );
		_view->horizontalHeader()->setSectionsMovable( false );

		// The viewport is what actually receives drag events.
		_view->viewport()->installEventFilter( this );

		connect( _view, &QTableView::doubleClicked, this, [this]( QModelIndex const& index ){
			Q_EMIT activated( index.data( PATH_ROLE ).toString(), index.data( IS_DIRECTORY_ROLE ).toBool() );
		} );
		connect( _view, &QTableView::customContextMenuRequested, this, [this]( QPoint const& position ){
			Q_EMIT context_menu_requested( _view->viewport()->mapToGlobal( position ) );
		} );
	}

	void FileTableView::set_model( FileListModel* model ){
		_model = model;
		_proxy->setSourceModel( model );
		_view->setModel( _proxy );

		_view->sortByColumn( static_cast<int>( FileColumn::NAME ), Qt::AscendingOrder );

		QHeaderView* header = _view->horizontalHeader();
		header->setSectionResizeMode( static_cast<int>( FileColumn::NAME ), QHeaderView::Stretch );
		header->setSectionResizeMode( static_cast<int>( FileColumn::SIZE ), QHeaderView::ResizeToContents );
		header->setSectionResizeMode( static_cast<int>( FileColumn::MODIFIED ), QHeaderView::ResizeToContents );
		header->setSectionResizeMode( static_cast<int>( FileColumn::PERMISSIONS ), QHeaderView::ResizeToContents );

		connect( _view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
				 [this] { Q_EMIT selection_changed(); } );
	}

	QStringList FileTableView::selected_paths() const{
		QStringList paths;
		if( _view->selectionModel() == nullptr )
			return paths;

		QModelIndexList const rows = _view->selectionModel()->selectedRows();
		paths.reserve( rows.size() );
		for( QModelIndex const& index : rows ){
			QString const path = index.data( PATH_ROLE ).toString();
			if( !path.isEmpty() )
				paths << path;
		}
		return paths;
	}

	bool FileTableView::has_selection() const{
		return _view->selectionModel() != nullptr && _view->selectionModel()->hasSelection();
	}

	void FileTableView::set_name_filter( QString const& pattern ){
		_proxy->setFilterRegularExpression(
			QRegularExpression( QRegularExpression::escape( pattern ), QRegularExpression::CaseInsensitiveOption ) );
	}

	QString FileTableView::drop_target_directory( QPoint const& position ) const{
		if( _model == nullptr )
			return {};

		QModelIndex const index = _view->indexAt( position );
		if( index.isValid() && index.data( IS_DIRECTORY_ROLE ).toBool() )
			return index.data( PATH_ROLE ).toString();

		return _model->current_path();
	}

	bool FileTableView::eventFilter( QObject* watched, QEvent* event ){
		if( watched != _view->viewport() )
			return QWidget::eventFilter( watched, event );

		switch( event->type() ){
			case QEvent::DragEnter:{
				auto*      drag_event = static_cast<QDragEnterEvent*>( event );
				bool const acceptable = drag_event->mimeData()->hasUrls() ||
										drag_event->mimeData()->hasFormat( QLatin1String( REMOTE_FILES_MIME_TYPE ) );
				if( acceptable )
					drag_event->acceptProposedAction();
				else
					drag_event->ignore();
				return true;
			}

			case QEvent::DragMove:
				handle_drag_move( static_cast<QDragMoveEvent*>( event ) );
				return true;

			case QEvent::DragLeave:
				_drop_row = -1;
				_view->viewport()->update();
				return true;

			case QEvent::Drop:
				handle_drop( static_cast<QDropEvent*>( event ) );
				return true;

			default:
				return QWidget::eventFilter( watched, event );
		}
	}

	void FileTableView::handle_drag_move( QDragMoveEvent* event ){
		QModelIndex const index = _view->indexAt( event->position().toPoint() );
		int const         row   = ( index.isValid() && index.data( IS_DIRECTORY_ROLE ).toBool() ) ? index.row() : -1;

		if( row != _drop_row ){
			_drop_row = row;
			// Highlight the folder that would receive the drop.
			_view->selectionModel()->clearCurrentIndex();
			if( row >= 0 )
				_view->setCurrentIndex( _proxy->index( row, 0 ) );
			_view->viewport()->update();
		}

		event->acceptProposedAction();
	}

	void FileTableView::handle_drop( QDropEvent* event ){
		QString const target = drop_target_directory( event->position().toPoint() );
		qCDebug( lc_files ) << "drop on" << ( _model && _model->is_remote() ? "remote" : "local" ) << "target" << target
							<< "formats" << event->mimeData()->formats();
		_drop_row = -1;

		if( target.isEmpty() ){
			event->ignore();
			return;
		}

		RemoteDragPayload const remote = remote_payload( event->mimeData() );
		if( remote.is_valid() ){
			Q_EMIT remote_files_dropped( remote, target );
			event->acceptProposedAction();
			return;
		}

		QStringList const paths = local_paths( event->mimeData() );
		if( !paths.isEmpty() ){
			Q_EMIT local_files_dropped( paths, target );
			event->acceptProposedAction();
			return;
		}

		event->ignore();
	}

	// ---------------------------------------------------------------------------
	// FilePane
	// ---------------------------------------------------------------------------

	FilePane::FilePane( QWidget* parent )
		: QWidget( parent )
	{
		build_ui();
	}

	void FilePane::build_ui(){
		setObjectName( QStringLiteral( "filePane" ) );

		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 0, 0, 0, 0 );
		root->setSpacing( 0 );

		// -- Header ------------------------------------------------------------
		auto* header = new QWidget( this );
		header->setObjectName( QStringLiteral( "filePaneHeader" ) );
		auto* header_layout = new QHBoxLayout( header );
		header_layout->setContentsMargins( 10, 7, 10, 7 );
		header_layout->setSpacing( 6 );

		auto const make_button = [header]( QString const& icon, QString const& tooltip ){
			auto* button = new QToolButton( header );
			button->setIcon( ui::icon( icon ) );
			button->setToolTip( tooltip );
			button->setAutoRaise( true );
			button->setIconSize( QSize( 16, 16 ) );
			return button;
		};

		_back_button    = make_button( QStringLiteral( "chevron-left" ), tr( "Back" ) );
		_forward_button = make_button( QStringLiteral( "chevron-right" ), tr( "Forward" ) );
		_up_button      = make_button( QStringLiteral( "arrow-up" ), tr( "Parent folder" ) );

		_title_label = new QLabel( header );
		_title_label->setObjectName( QStringLiteral( "filePaneTitle" ) );

		_path_edit = new QLineEdit( header );
		_path_edit->setObjectName( QStringLiteral( "pathEdit" ) );
		_path_edit->setClearButtonEnabled( false );
		_path_edit->setPlaceholderText( tr( "Path" ) );

		auto* refresh_button    = make_button( QStringLiteral( "refresh" ), tr( "Refresh" ) );
		auto* new_folder_button = make_button( QStringLiteral( "folder-plus" ), tr( "New folder" ) );
		_hidden_button          = make_button( QStringLiteral( "eye" ), tr( "Show hidden files" ) );
		_hidden_button->setCheckable( true );

		header_layout->addWidget( _back_button );
		header_layout->addWidget( _forward_button );
		header_layout->addWidget( _up_button );
		header_layout->addWidget( _path_edit, 1 );
		header_layout->addWidget( refresh_button );
		header_layout->addWidget( new_folder_button );
		header_layout->addWidget( _hidden_button );

		// -- Title strip -------------------------------------------------------
		auto* title_strip = new QWidget( this );
		title_strip->setObjectName( QStringLiteral( "filePaneTitleStrip" ) );
		auto* title_layout = new QHBoxLayout( title_strip );
		title_layout->setContentsMargins( 12, 6, 10, 6 );
		title_layout->setSpacing( 8 );
		title_layout->addWidget( _title_label );
		title_layout->addStretch( 1 );

		_filter_edit = new QLineEdit( title_strip );
		_filter_edit->setObjectName( QStringLiteral( "filterEdit" ) );
		_filter_edit->setPlaceholderText( tr( "Filter" ) );
		_filter_edit->setClearButtonEnabled( true );
		_filter_edit->setMaximumWidth( 180 );
		title_layout->addWidget( _filter_edit );

		// -- Listing -----------------------------------------------------------
		_table = new FileTableView( this );

		_placeholder_label = new QLabel( this );
		_placeholder_label->setObjectName( QStringLiteral( "filePanePlaceholder" ) );
		_placeholder_label->setAlignment( Qt::AlignCenter );
		_placeholder_label->setWordWrap( true );
		_placeholder_label->hide();

		auto* stack        = new QWidget( this );
		auto* stack_layout = new QStackedLayout( stack );
		stack_layout->setContentsMargins( 0, 0, 0, 0 );
		stack_layout->setStackingMode( QStackedLayout::StackAll );
		stack_layout->addWidget( _table );
		stack_layout->addWidget( _placeholder_label );

		// -- Status ------------------------------------------------------------
		_status_label = new QLabel( this );
		_status_label->setObjectName( QStringLiteral( "filePaneStatus" ) );
		_status_label->setContentsMargins( 12, 4, 12, 4 );

		root->addWidget( title_strip );
		root->addWidget( header );
		root->addWidget( stack, 1 );
		root->addWidget( _status_label );

		// -- Wiring ------------------------------------------------------------
		connect( _back_button, &QToolButton::clicked, this, &FilePane::navigate_back );
		connect( _forward_button, &QToolButton::clicked, this, &FilePane::navigate_forward );
		connect( _up_button, &QToolButton::clicked, this, &FilePane::navigate_up );
		connect( refresh_button, &QToolButton::clicked, this, &FilePane::refresh );
		connect( new_folder_button, &QToolButton::clicked, this, &FilePane::create_folder );
		connect( _hidden_button, &QToolButton::toggled, this, &FilePane::toggle_hidden_files );

		connect( _path_edit, &QLineEdit::returnPressed, this, [this]{
			if( _model != nullptr )
				_model->set_current_path( _path_edit->text().trimmed() );
		} );

		connect( _filter_edit, &QLineEdit::textChanged, this, [this]( QString const& text ){
			_table->set_name_filter( text );
			update_status();
		} );

		connect( _table, &FileTableView::activated, this, [this]( QString const& path, bool is_directory ){
			if( is_directory && _model != nullptr )
				_model->set_current_path( path );
			else
				Q_EMIT transfer_requested( { path } );
		} );

		connect( _table, &FileTableView::local_files_dropped, this, &FilePane::local_files_dropped );
		connect( _table, &FileTableView::remote_files_dropped, this, &FilePane::remote_files_dropped );
		connect( _table, &FileTableView::selection_changed, this, [this]{
			update_status();
			Q_EMIT selection_changed();
		} );
		connect( _table, &FileTableView::context_menu_requested, this, &FilePane::show_context_menu );

		_back_button->setEnabled( false );
		_forward_button->setEnabled( false );
	}

	void FilePane::set_model( FileListModel* model ){
		_model = model;
		_table->set_model( model );

		connect( model, &FileListModel::path_changed, this, &FilePane::apply_path );
		connect( model, &FileListModel::error_occurred, this, [this]( QString const& message ){
			_status_label->setText( message );
			_status_label->setProperty( "state", QStringLiteral( "error" ) );
			_status_label->style()->polish( _status_label );
		} );
		connect( model, &FileListModel::loading_changed, this, [this]( bool loading ){
			if( loading )
				_status_label->setText( tr( "Loading…" ) );
			else
				update_status();
		} );
		connect( model, &QAbstractItemModel::modelReset, this, &FilePane::update_status );

		_hidden_button->setChecked( model->shows_hidden() );
		apply_path( model->current_path() );
	}

	void FilePane::set_title( QString const& title ){
		_title_label->setText( title );
	}

	void FilePane::set_placeholder( QString const& message ){
		_placeholder_label->setText( message );
		_placeholder_label->show();
		_placeholder_label->raise();
		_table->setEnabled( false );
	}

	void FilePane::clear_placeholder(){
		_placeholder_label->hide();
		_table->setEnabled( true );
	}

	QString FilePane::current_directory() const{
		return _model != nullptr ? _model->current_path() : QString();
	}

	QStringList FilePane::selected_paths() const{
		return _table->selected_paths();
	}

	void FilePane::apply_path( QString const& path ){
		_path_edit->setText( path );

		if( !_navigating_history )
			push_history( path );

		_up_button->setEnabled( _model != nullptr && !_model->parent_path().isEmpty() );
		_back_button->setEnabled( _history_index > 0 );
		_forward_button->setEnabled( _history_index >= 0 && _history_index < _history.size() - 1 );

		update_status();
	}

	void FilePane::push_history( QString const& path ){
		if( _history_index >= 0 && _history_index < _history.size() && _history.at( _history_index ) == path ){
			return;
		}

		// A new destination truncates the forward history, exactly like a browser.
		while( _history.size() > _history_index + 1 )
			_history.removeLast();

		_history.append( path );
		_history_index = static_cast<int>( _history.size() ) - 1;
	}

	void FilePane::navigate_back(){
		if( _history_index <= 0 || _model == nullptr )
			return;

		--_history_index;
		_navigating_history = true;
		_model->set_current_path( _history.at( _history_index ) );
		_navigating_history = false;
	}

	void FilePane::navigate_forward(){
		if( _model == nullptr || _history_index < 0 || _history_index >= _history.size() - 1 )
			return;

		++_history_index;
		_navigating_history = true;
		_model->set_current_path( _history.at( _history_index ) );
		_navigating_history = false;
	}

	void FilePane::navigate_up(){
		if( _model == nullptr )
			return;
		QString const parent = _model->parent_path();
		if( !parent.isEmpty() )
			_model->set_current_path( parent );
	}

	void FilePane::navigate_home(){
		if( _model == nullptr )
			return;
		_model->set_current_path( _model->is_remote() ? QStringLiteral( "~" ) : QDir::homePath() );
	}

	void FilePane::refresh(){
		if( _model != nullptr )
			_model->refresh();
	}

	void FilePane::toggle_hidden_files(){
		if( _model != nullptr )
			_model->set_show_hidden( _hidden_button->isChecked() );
	}

	void FilePane::create_folder(){
		if( _model == nullptr )
			return;

		bool          accepted = false;
		QString const name = QInputDialog::getText( this, tr( "New Folder" ), tr( "Folder name:" ), QLineEdit::Normal,
													tr( "untitled folder" ), &accepted );
		if( !accepted || name.trimmed().isEmpty() )
			return;

		_model->create_directory( name.trimmed() );
	}

	void FilePane::rename_selection(){
		if( _model == nullptr )
			return;

		QStringList const paths = selected_paths();
		if( paths.size() != 1 )
			return;

		QString const current_name = paths.first().section( QLatin1Char( '/' ), -1 );

		bool          accepted = false;
		QString const name     = QInputDialog::getText( this, tr( "Rename" ), tr( "New name:" ), QLineEdit::Normal,
														current_name, &accepted );
		if( !accepted || name.trimmed().isEmpty() || name == current_name )
			return;

		_model->rename( paths.first(), name.trimmed() );
	}

	void FilePane::delete_selection(){
		if( _model == nullptr )
			return;

		QStringList const paths = selected_paths();
		if( paths.isEmpty() )
			return;

		QString const question = paths.size() == 1
									 ? tr( "Delete “%1”?" ).arg( paths.first().section( QLatin1Char( '/' ), -1 ) )
									 : tr( "Delete %n selected items?", nullptr, paths.size() );

		auto const answer = QMessageBox::warning( this, tr( "Delete" ), question,
												  QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel );
		if( answer != QMessageBox::Yes )
			return;

		_model->remove( paths );
	}

	void FilePane::copy_path_to_clipboard(){
		QStringList const paths = selected_paths();
		if( paths.isEmpty() ){
			QGuiApplication::clipboard()->setText( current_directory() );
			return;
		}
		QGuiApplication::clipboard()->setText( paths.join( QLatin1Char( '\n' ) ) );
	}

	void FilePane::show_context_menu( QPoint const& global_position ){
		if( _model == nullptr )
			return;

		QStringList const paths         = selected_paths();
		bool const        has_selection = !paths.isEmpty();

		QMenu menu( this );

		if( has_selection ){
			QString const label = _model->is_remote() ? tr( "Download" ) : tr( "Upload" );
			menu.addAction( label, this, [this, paths] { Q_EMIT transfer_requested( paths ); } );
			menu.addSeparator();
		}

		menu.addAction( tr( "New Folder…" ), this, &FilePane::create_folder );

		QAction* rename_action = menu.addAction( tr( "Rename…" ), this, &FilePane::rename_selection );
		rename_action->setEnabled( paths.size() == 1 );

		QAction* delete_action = menu.addAction( tr( "Delete…" ), this, &FilePane::delete_selection );
		delete_action->setEnabled( has_selection );

		menu.addSeparator();
		menu.addAction( tr( "Copy Path" ), this, &FilePane::copy_path_to_clipboard );
		menu.addAction( tr( "Refresh" ), this, &FilePane::refresh );

		menu.exec( global_position );
	}

	void FilePane::update_status(){
		if( _model == nullptr )
			return;

		_status_label->setProperty( "state", QString() );
		_status_label->style()->polish( _status_label );

		int const         shown    = _table->proxy()->rowCount();
		QStringList const selected = selected_paths();

		if( selected.isEmpty() ){
			_status_label->setText( tr( "%n item(s)", nullptr, shown ) );
			return;
		}

		quint64               total_bytes = 0;
		QModelIndexList const rows        = _table->view()->selectionModel()->selectedRows();
		for( QModelIndex const& index : rows )
			total_bytes += index.data( SIZE_ROLE ).toULongLong();

		// QStringLiteral, not QLatin1String: the separator is non-ASCII and
		// Latin-1 would mangle its UTF-8 bytes.
		_status_label->setText( tr( "%n selected", nullptr, selected.size() ) + QStringLiteral( " · " ) +
								format_file_size( total_bytes ) );
	}

} // namespace arterm::files
