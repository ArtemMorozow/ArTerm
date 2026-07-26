#include "files/transfer_panel.hpp"

#include "files/transfer_manager.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace arterm::files
{
	namespace
	{

		constexpr int COLLAPSED_HEIGHT = 30;
		constexpr int EXPANDED_HEIGHT  = 168;

	} // namespace

	void TransferProgressDelegate::paint( QPainter* painter, QStyleOptionViewItem const& option,
										  QModelIndex const& index ) const{
		auto const   state    = static_cast<ssh::TransferState>( index.data( TransferManager::STATE_ROLE ).toInt() );
		double const fraction = index.data( TransferManager::FRACTION_ROLE ).toDouble();

		QStyleOptionViewItem adjusted = option;
		initStyleOption( &adjusted, index );

		painter->save();
		if( option.state & QStyle::State_Selected )
			painter->fillRect( option.rect, option.palette.highlight() );

		ui::Theme const& theme = ui::Theme::current();

		// The byte counts need room to the right of the bar; without this reserve
		// the text is clipped to "195 KB / 195".
		constexpr int TEXT_WIDTH  = 140;
		qreal const   track_width = std::max( 24.0, static_cast<qreal>( option.rect.width() - TEXT_WIDTH - 16 ) );
		QRectF const  track( option.rect.left() + 8, option.rect.center().y() - 3, track_width, 6 );

		painter->setRenderHint( QPainter::Antialiasing, true );
		painter->setPen( Qt::NoPen );
		painter->setBrush( theme.elevated );
		painter->drawRoundedRect( track, 3, 3 );

		QColor fill_color = theme.accent;
		switch( state ){
			case ssh::TransferState::COMPLETED:
				fill_color = theme.success;
				break;
			case ssh::TransferState::FAILED:
				fill_color = theme.danger;
				break;
			case ssh::TransferState::CANCELLED:
				fill_color = theme.text_muted;
				break;
			default:
				break;
		}

		double const clamped = std::clamp( fraction, 0.0, 1.0 );
		if( clamped > 0.0 ){
			QRectF fill = track;
			fill.setWidth( track.width() * clamped );
			painter->setBrush( fill_color );
			painter->drawRoundedRect( fill, 3, 3 );
		}

		// The byte counts the model formats sit to the right of the bar.
		painter->setPen( theme.text_muted );
		QRect const text_rect( static_cast<int>( track.right() ) + 8, option.rect.top(), TEXT_WIDTH,
							   option.rect.height() );
		painter->drawText( text_rect, Qt::AlignVCenter | Qt::AlignLeft, index.data( Qt::DisplayRole ).toString() );

		painter->restore();
	}

	QSize TransferProgressDelegate::sizeHint( QStyleOptionViewItem const& option, QModelIndex const& index ) const{
		QSize size = QStyledItemDelegate::sizeHint( option, index );
		size.setWidth( std::max( size.width(), 240 ) );
		return size;
	}

	TransferPanel::TransferPanel( TransferManager* manager, QWidget* parent )
		: QWidget( parent )
		, _manager( manager )
	{
		setObjectName( QStringLiteral( "transferPanel" ) );

		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 0, 0, 0, 0 );
		root->setSpacing( 0 );

		// -- Header ------------------------------------------------------------
		auto* header        = new QWidget( this );
		auto* header_layout = new QHBoxLayout( header );
		header_layout->setContentsMargins( 12, 4, 8, 4 );
		header_layout->setSpacing( 6 );

		_toggle_button = new QToolButton( header );
		_toggle_button->setIcon( ui::icon( QStringLiteral( "chevron-down" ) ) );
		_toggle_button->setAutoRaise( true );
		_toggle_button->setIconSize( QSize( 14, 14 ) );

		_header_label = new QLabel( header );
		_header_label->setObjectName( QStringLiteral( "transferPanelHeader" ) );

		_cancel_button = new QToolButton( header );
		_cancel_button->setIcon( ui::icon( QStringLiteral( "close" ) ) );
		_cancel_button->setToolTip( tr( "Cancel all transfers" ) );
		_cancel_button->setAutoRaise( true );
		_cancel_button->setIconSize( QSize( 14, 14 ) );

		_clear_button = new QToolButton( header );
		_clear_button->setIcon( ui::icon( QStringLiteral( "trash" ) ) );
		_clear_button->setToolTip( tr( "Clear finished transfers" ) );
		_clear_button->setAutoRaise( true );
		_clear_button->setIconSize( QSize( 14, 14 ) );

		header_layout->addWidget( _toggle_button );
		header_layout->addWidget( _header_label, 1 );
		header_layout->addWidget( _cancel_button );
		header_layout->addWidget( _clear_button );

		// -- Table -------------------------------------------------------------
		_table = new QTableView( this );
		_table->setObjectName( QStringLiteral( "transferTable" ) );
		_table->setModel( _manager );
		_table->setSelectionBehavior( QAbstractItemView::SelectRows );
		_table->setSelectionMode( QAbstractItemView::SingleSelection );
		_table->setShowGrid( false );
		_table->setFrameStyle( QFrame::NoFrame );
		_table->setEditTriggers( QAbstractItemView::NoEditTriggers );
		_table->verticalHeader()->setVisible( false );
		_table->verticalHeader()->setDefaultSectionSize( 26 );
		_table->horizontalHeader()->setHighlightSections( false );
		_table->setItemDelegateForColumn( static_cast<int>( TransferManager::Column::PROGRESS ),
										  new TransferProgressDelegate( this ) );

		_table->horizontalHeader()->setSectionResizeMode( static_cast<int>( TransferManager::Column::NAME ),
														  QHeaderView::Stretch );

		root->addWidget( header );
		root->addWidget( _table, 1 );

		connect( _toggle_button, &QToolButton::clicked, this, [this]{
			if( _expanded )
				collapse();
			else
				expand();
		} );
		connect( _clear_button, &QToolButton::clicked, _manager, &TransferManager::clear_completed );
		connect( _cancel_button, &QToolButton::clicked, _manager, &TransferManager::cancel_all );

		connect( _manager, &QAbstractItemModel::rowsInserted, this, [this]{
			update_header();
			expand();
		} );
		connect( _manager, &QAbstractItemModel::rowsRemoved, this, &TransferPanel::update_header );
		connect( _manager, &QAbstractItemModel::dataChanged, this, &TransferPanel::update_header );

		collapse();
		update_header();
	}

	int TransferPanel::header_height() noexcept{
		return COLLAPSED_HEIGHT;
	}

	void TransferPanel::expand(){
		if( _expanded )
			return;

		_expanded = true;
		_table->show();
		_toggle_button->setIcon( ui::icon( QStringLiteral( "chevron-down" ) ) );
		setMinimumHeight( COLLAPSED_HEIGHT );
		setMaximumHeight( QWIDGETSIZE_MAX );

		Q_EMIT expanded_changed( true );
	}

	void TransferPanel::collapse(){
		_expanded = false;
		_table->hide();
		_toggle_button->setIcon( ui::icon( QStringLiteral( "chevron-right" ) ) );
		setMinimumHeight( COLLAPSED_HEIGHT );
		setMaximumHeight( COLLAPSED_HEIGHT );

		Q_EMIT expanded_changed( false );
	}

	void TransferPanel::update_header(){
		int const active = _manager->active_count();
		int const total  = _manager->rowCount();

		if( total == 0 ){
			_header_label->setText( tr( "TRANSFERS" ) );
			_cancel_button->setEnabled( false );
			_clear_button->setEnabled( false );
			return;
		}

		_header_label->setText( active > 0 ? tr( "TRANSFERS — %n active", nullptr, active )
										   : tr( "TRANSFERS — %n finished", nullptr, total ) );
		_cancel_button->setEnabled( active > 0 );
		_clear_button->setEnabled( active < total );
	}

} // namespace arterm::files
