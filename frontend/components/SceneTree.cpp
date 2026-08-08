#include "SceneTree.hpp"

#include <QScrollBar>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QContextMenuEvent>
#include <QCoreApplication>

#include <widgets/OBSQTDisplay.hpp>
#include <widgets/OBSBasic.hpp>
#include "moc_SceneTree.cpp"

SceneItemWidget::SceneItemWidget(OBSSource source_, QListWidgetItem *item_, QListWidget *list_, QWidget *parent_)
    : QWidget(parent_), source(source_), item(item_), list(list_)
{
    setAttribute(Qt::WA_StyledBackground, true);
    // Solid background to hide underlying QListWidgetItem text
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(43, 43, 43)); // Matches OBS dark theme
    setPalette(pal);

    setFixedSize(144, 81); // Set fixed box size
    setObjectName("SceneItemWidget");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    display = new OBSQTDisplay(this);
    display->setFixedSize(144, 81); // Approx 16:9 ratio for the thumbnail
    display->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    display->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(display);

    indexLabel = new QLabel("", this);
    indexLabel->setAlignment(Qt::AlignCenter);
    indexLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    
    deleteButton = new QPushButton(this);
    deleteButton->setText(""); 
    deleteButton->setToolTip(QTStr("Remove"));
    deleteButton->setFixedSize(22, 22);
    deleteButton->setIcon(QIcon("F:/Techrise/frontend/forms/images/icons8-delete-50.svg"));
    deleteButton->setIconSize(QSize(20, 20));
    deleteButton->setAttribute(Qt::WA_TranslucentBackground);
    deleteButton->setStyleSheet("background-color: transparent; border: none;");
    
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        if (list && item) list->setCurrentItem(item);
        QMetaObject::invokeMethod(OBSBasic::Get(), "on_actionRemoveScene_triggered", Qt::QueuedConnection);
    });
    
    deleteButton->hide();

	weakSource = obs_source_get_weak_source(source);

    // Setup OBS render for the display
    auto addDrawCallback = [this](OBSQTDisplay *) {
        obs_display_add_draw_callback(display->GetDisplay(), SceneItemWidget::DrawScene, this);
    };

    connect(display, &OBSQTDisplay::DisplayCreated, this, addDrawCallback);
    obs_source_inc_showing(source);
}

SceneItemWidget::~SceneItemWidget()
{
	if (display && display->GetDisplay()) {
		obs_display_remove_draw_callback(display->GetDisplay(), SceneItemWidget::DrawScene, this);
	}
	obs_source_t *s = obs_weak_source_get_source(weakSource);
	if (s) {
		obs_source_dec_showing(s);
		obs_source_release(s);
	}
	obs_weak_source_release(weakSource);
}

void SceneItemWidget::UpdateIndexAndSelection(int index, bool selected)
{
    indexLabel->setText(QString::number(index));
    
    if (selected) {
        indexLabel->setStyleSheet("color: white; background-color: #00A3FF; border-radius: 8px; padding: 0px 4px; font-weight: bold; font-size: 10px;");
        setStyleSheet("#SceneItemWidget { border: none; background-color: transparent; }");
    } else {
        indexLabel->setStyleSheet("color: white; background-color: #404040; border-radius: 8px; padding: 0px 4px; font-weight: bold; font-size: 10px;");
        setStyleSheet("#SceneItemWidget { border: none; background-color: transparent; }");
    }

    indexLabel->adjustSize();
    indexLabel->move(width() - indexLabel->width(), 0);
    indexLabel->raise();
}

void SceneItemWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (indexLabel) {
        indexLabel->adjustSize();
        indexLabel->move(width() - indexLabel->width(), 0);
    }
    if (deleteButton) {
        deleteButton->move(width() - deleteButton->width() - 5, height() - deleteButton->height() - 5);
    }
}

void SceneItemWidget::DrawScene(void *data, uint32_t cx, uint32_t cy)
{
	SceneItemWidget *widget = static_cast<SceneItemWidget *>(data);
	if (widget && widget->source) {
		uint32_t sourceCX = obs_source_get_width(widget->source);
		uint32_t sourceCY = obs_source_get_height(widget->source);
		
		if (sourceCX > 0 && sourceCY > 0) {
			float scaleX = (float)cx / (float)sourceCX;
			float scaleY = (float)cy / (float)sourceCY;
			float scale = scaleX < scaleY ? scaleX : scaleY;

			float xOffset = (cx - (sourceCX * scale)) / 2.0f;
			float yOffset = (cy - (sourceCY * scale)) / 2.0f;

			gs_matrix_push();
			gs_matrix_translate3f(xOffset, yOffset, 0.0f);
			gs_matrix_scale3f(scale, scale, 1.0f);
			obs_source_video_render(widget->source);
			gs_matrix_pop();
		} else {
			obs_source_video_render(widget->source);
		}
	}
}

void SceneItemWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && list && item) {
        list->setCurrentItem(item);
    } else if (event->button() == Qt::RightButton && list && item) {
        list->setCurrentItem(item);
        QPoint globalPos = QCursor::pos();
        QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, event->pos(), globalPos);
        QCoreApplication::sendEvent(list->viewport(), &contextEvent);
    }
    QWidget::mousePressEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void SceneItemWidget::enterEvent(QEnterEvent *event)
#else
void SceneItemWidget::enterEvent(QEvent *event)
#endif
{
    if (deleteButton) deleteButton->show();
    QWidget::enterEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void SceneItemWidget::leaveEvent(QEvent *event)
#else
void SceneItemWidget::leaveEvent(QEvent *event)
#endif
{
    if (deleteButton) deleteButton->hide();
    QWidget::leaveEvent(event);
}

SceneTree::SceneTree(QWidget *parent_) : QListWidget(parent_)
{
	this->setStyleSheet(
		"QListWidget { background-color: transparent; border: none; }"
		"QListWidget::item { background: transparent; color: transparent; border: none; }"
		"QListWidget::item:selected { background: transparent; color: transparent; border: none; }"
		"QListWidget::item:hover { background: transparent; color: transparent; border: none; }"
	);

	thumbnailScrollArea = new QScrollArea(this);
	thumbnailScrollArea->setStyleSheet("QScrollArea { background-color: palette(window); border: none; } QWidget#thumbnailContainer { background-color: transparent; }");
	thumbnailScrollArea->setWidgetResizable(true);
	thumbnailScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	thumbnailScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	thumbnailScrollArea->setFixedHeight(81); 
	thumbnailScrollArea->setFrameShape(QFrame::NoFrame);
	thumbnailContainer = new QWidget();
	thumbnailLayout = new QHBoxLayout(thumbnailContainer);
	thumbnailLayout->setContentsMargins(8, 0, 8, 0);
	thumbnailLayout->setSpacing(8);
	thumbnailLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	thumbnailScrollArea->setWidget(thumbnailContainer);
	addButton = new QPushButton("+", this);
	addButton->setFixedSize(144, 81);
	addButton->setStyleSheet("QPushButton { color: white; font-size: 32px; font-weight: bold; background-color: #2b2b2b; border: 1px solid #444; border-radius: 4px; margin: 0px; }");
	thumbnailLayout->addWidget(addButton, 0, Qt::AlignTop);
	
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->addWidget(thumbnailScrollArea);
	setLayout(mainLayout);
	
	installEventFilter(this);
	setDragDropMode(InternalMove);
	setMovement(QListView::Snap);
	
	setMinimumHeight(81); // Prevent clipping when the dock shrinks

	scrollLeftBtn = new QPushButton("<", this);
	scrollLeftBtn->setFixedSize(30, 60);
	scrollLeftBtn->setStyleSheet("QPushButton { background-color: rgba(140, 160, 170, 0.8); color: black; border-top-right-radius: 30px; border-bottom-right-radius: 30px; font-size: 16px; font-weight: bold; border: none; padding: 0px; } QPushButton:hover { background-color: rgba(140, 160, 170, 1.0); }");
	scrollLeftBtn->hide();

	scrollRightBtn = new QPushButton(">", this);
	scrollRightBtn->setFixedSize(30, 60);
	scrollRightBtn->setStyleSheet("QPushButton { background-color: rgba(140, 160, 170, 0.8); color: black; border-top-left-radius: 30px; border-bottom-left-radius: 30px; font-size: 16px; font-weight: bold; border: none; padding: 0px; } QPushButton:hover { background-color: rgba(140, 160, 170, 1.0); }");
	scrollRightBtn->hide();

	connect(addButton, &QPushButton::clicked, this, [this]() {
		QMetaObject::invokeMethod(OBSBasic::Get(), "on_actionAddScene_triggered", Qt::QueuedConnection);
	});
	connect(scrollLeftBtn, &QPushButton::clicked, this, [this]() {
		QScrollBar *hb = thumbnailScrollArea->horizontalScrollBar();
		hb->setValue(hb->value() - 200);
	});
	connect(scrollRightBtn, &QPushButton::clicked, this, [this]() {
		QScrollBar *hb = thumbnailScrollArea->horizontalScrollBar();
		hb->setValue(hb->value() + 200);
	});
	connect(thumbnailScrollArea->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &SceneTree::UpdateScrollButtons);
	connect(thumbnailScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this, &SceneTree::UpdateScrollButtons);

	QResizeEvent resEvent(size(), size());
	SceneTree::resizeEvent(&resEvent);
	
	SyncThumbnailLayout();
	QTimer::singleShot(100, [this]() { emit scenesReordered(); });
	connect(this, &QListWidget::itemSelectionChanged, this, &SceneTree::SyncThumbnailLayout);
}

void SceneTree::EnsureAddSceneItem()
{
	// No longer needed, button is now part of layout
}

void SceneTree::SyncThumbnailLayout()
{
	// Reorder items in thumbnailLayout to match QListWidget items
	for (int i = 0; i < count(); ++i) {
		QListWidgetItem *listItem = item(i);
		listItem->setSizeHint(QSize(0, 0)); // force height to 0!
		for (int j = 0; j < thumbnailLayout->count(); ++j) {
			QLayoutItem *layoutItem = thumbnailLayout->itemAt(j);
			if (layoutItem) {
				SceneItemWidget *sceneWidget = qobject_cast<SceneItemWidget*>(layoutItem->widget());
				if (sceneWidget && sceneWidget->GetItem() == listItem) {
					thumbnailLayout->insertWidget(i, sceneWidget, 0, Qt::AlignTop);
					sceneWidget->UpdateIndexAndSelection(i + 1, listItem->isSelected());
					break;
				}
			}
		}
	}
	thumbnailLayout->removeWidget(addButton);
	thumbnailLayout->addWidget(addButton, 0, Qt::AlignTop);
	UpdateScrollButtons();
}

AddSceneItemWidget::AddSceneItemWidget(QWidget *parent) : QWidget(parent)
{
	layout = new QVBoxLayout(this);
	label = new QLabel("+", this);
	layout->addWidget(label);
}

void SceneTree::SetGridMode(bool grid)
{
	parent()->setProperty("class", grid ? "list-grid" : "");
	gridMode = grid;

	if (gridMode) {
		setResizeMode(QListView::Adjust);
		setViewMode(QListView::IconMode);
		setUniformItemSizes(true);
	} else {
		setViewMode(QListView::ListMode);
		setResizeMode(QListView::Fixed);
	}

	this->setStyleSheet(
		"QListWidget { background-color: transparent; border: none; }"
		"QListWidget::item { background: transparent; color: transparent; border: none; }"
		"QListWidget::item:selected { background: transparent; color: transparent; border: none; }"
		"QListWidget::item:hover { background: transparent; color: transparent; border: none; }"
	);

	QResizeEvent event(size(), size());
	resizeEvent(&event);
}

bool SceneTree::GetGridMode()
{
	return gridMode;
}

void SceneTree::SetGridItemWidth(int width)
{
	maxWidth = width;
}

void SceneTree::SetGridItemHeight(int height)
{
	itemHeight = height;
}

int SceneTree::GetGridItemWidth()
{
	return maxWidth;
}

int SceneTree::GetGridItemHeight()
{
	return itemHeight;
}

bool SceneTree::eventFilter(QObject *obj, QEvent *event)
{
	return QObject::eventFilter(obj, event);
}

void SceneTree::UpdateScrollButtons()
{
	QScrollBar *hb = thumbnailScrollArea->horizontalScrollBar();
	if (hb->minimum() == hb->maximum()) {
		scrollLeftBtn->hide();
		scrollRightBtn->hide();
	} else {
		scrollLeftBtn->setVisible(hb->value() > hb->minimum());
		scrollRightBtn->setVisible(hb->value() < hb->maximum());
		scrollLeftBtn->raise();
		scrollRightBtn->raise();
	}
}

void SceneTree::resizeEvent(QResizeEvent *event)
{
	scrollLeftBtn->move(0, (height() - scrollLeftBtn->height()) / 2);
	scrollRightBtn->move(width() - scrollRightBtn->width(), (height() - scrollRightBtn->height()) / 2);

	if (gridMode) {
		int scrollWid = verticalScrollBar()->sizeHint().width();
		const QRect lastItem = visualItemRect(item(count() - 1));
		const int h = lastItem.y() + lastItem.height();

		if (h < height()) {
			setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			scrollWid = 0;
		} else {
			setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		}

		int wid = contentsRect().width() - scrollWid - 1;
		int items = (int)std::ceil((float)wid / maxWidth);
		int itemWidth = wid / items;

		setGridSize(QSize(itemWidth, itemHeight));
	} else {
		setGridSize(QSize());
	}

	for (int i = 0; i < count(); i++) {
		item(i)->setSizeHint(QSize(0, 0));
	}

	QListWidget::resizeEvent(event);
}

void SceneTree::startDrag(Qt::DropActions supportedActions)
{
	QListWidget::startDrag(supportedActions);
}

void SceneTree::dropEvent(QDropEvent *event)
{
	if (event->source() != this) {
		QListWidget::dropEvent(event);
		return;
	}

	if (gridMode) {
		int scrollWid = verticalScrollBar()->sizeHint().width();
		const QRect firstItem = visualItemRect(item(0));
		const QRect lastItem = visualItemRect(item(count() - 1));
		const int h = lastItem.y() + lastItem.height();
		const int firstItemY = abs(firstItem.y());

		if (h < height()) {
			setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			scrollWid = 0;
		} else {
			setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		}

		float wid = contentsRect().width() - scrollWid - 1;

		QPoint point = event->position().toPoint();

		int x = (float)point.x() / wid * std::ceil(wid / maxWidth);
		int y = (point.y() + firstItemY) / itemHeight;

		int r = x + y * std::ceil(wid / maxWidth);

		QListWidgetItem *item = takeItem(selectedIndexes()[0].row());
		insertItem(r, item);
		setCurrentItem(item);
		resize(size());
	}

	QListWidget::dropEvent(event);

	// We must call resizeEvent to correctly place all grid items.
	// We also do this in rowsInserted.
	QResizeEvent resEvent(size(), size());
	SceneTree::resizeEvent(&resEvent);

	QTimer::singleShot(100, [this]() { emit scenesReordered(); });
}

void SceneTree::RepositionGrid(QDragMoveEvent *event)
{
	int scrollWid = verticalScrollBar()->sizeHint().width();
	const QRect firstItem = visualItemRect(item(0));
	const QRect lastItem = visualItemRect(item(count() - 1));
	const int h = lastItem.y() + lastItem.height();
	const int firstItemY = abs(firstItem.y());

	if (h < height()) {
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scrollWid = 0;
	} else {
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	}

	float wid = contentsRect().width() - scrollWid - 1;

	if (event) {
		QPoint point = event->position().toPoint();

		int x = (float)point.x() / wid * std::ceil(wid / maxWidth);
		int y = (point.y() + firstItemY) / itemHeight;

		int r = x + y * std::ceil(wid / maxWidth);
		int orig = selectedIndexes()[0].row();

		for (int i = 0; i < count(); i++) {
			auto *wItem = item(i);

			if (wItem->isSelected())
				continue;

			QModelIndex index = indexFromItem(wItem);

			int off = (i >= r ? 1 : 0) - (i > orig && i > r ? 1 : 0) - (i > orig && i == r ? 2 : 0);

			int xPos = (i + off) % (int)std::ceil(wid / maxWidth);
			int yPos = (i + off) / (int)std::ceil(wid / maxWidth);
			QSize g = gridSize();

			QPoint position(xPos * g.width(), yPos * g.height());
			setPositionForIndex(position, index);
		}
	} else {
		for (int i = 0; i < count(); i++) {
			auto *wItem = item(i);

			if (wItem->isSelected())
				continue;

			QModelIndex index = indexFromItem(wItem);

			int xPos = i % (int)std::ceil(wid / maxWidth);
			int yPos = i / (int)std::ceil(wid / maxWidth);
			QSize g = gridSize();

			QPoint position(xPos * g.width(), yPos * g.height());
			setPositionForIndex(position, index);
		}
	}
}

void SceneTree::dragMoveEvent(QDragMoveEvent *event)
{
	if (gridMode) {
		RepositionGrid(event);
	}

	QListWidget::dragMoveEvent(event);
}

void SceneTree::dragLeaveEvent(QDragLeaveEvent *event)
{
	if (gridMode) {
		RepositionGrid();
	}

	QListWidget::dragLeaveEvent(event);
}

void SceneTree::rowsInserted(const QModelIndex &parent, int start, int end)
{
	QResizeEvent event(size(), size());
	SceneTree::resizeEvent(&event);

	QListWidget::rowsInserted(parent, start, end);
}

#if QT_VERSION < QT_VERSION_CHECK(6, 4, 3)
// Workaround for QTBUG-105870. Remove once that is solved upstream.
void SceneTree::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
	if (selected.count() == 0 && deselected.count() > 0 && !property("clearing").toBool())
		setCurrentRow(deselected.indexes().front().row());
}
#endif
