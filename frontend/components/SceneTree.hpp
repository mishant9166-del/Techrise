#pragma once

#include <QListWidget>
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QResizeEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QMouseEvent>
#include <obs.hpp>

class OBSQTDisplay;

class SceneTree : public QListWidget {
	Q_OBJECT
	Q_PROPERTY(int gridItemWidth READ GetGridItemWidth WRITE SetGridItemWidth DESIGNABLE true)
	Q_PROPERTY(int gridItemHeight READ GetGridItemHeight WRITE SetGridItemHeight DESIGNABLE true)

	bool gridMode = false;
	int maxWidth = 150;
	int itemHeight = 24;

public:
	QScrollArea *thumbnailScrollArea = nullptr;
	QWidget *thumbnailContainer = nullptr;
	QHBoxLayout *thumbnailLayout = nullptr;
	QPushButton *addButton = nullptr;
	void SetGridMode(bool grid);
	bool GetGridMode();

	void SetGridItemWidth(int width);
	void SetGridItemHeight(int height);
	int GetGridItemWidth();
	int GetGridItemHeight();
	void EnsureAddSceneItem();
	void SyncThumbnailLayout();
	void UpdateScrollButtons();

	QPushButton *scrollLeftBtn = nullptr;
	QPushButton *scrollRightBtn = nullptr;

	explicit SceneTree(QWidget *parent = nullptr);

private:
	void RepositionGrid(QDragMoveEvent *event = nullptr);

protected:
	virtual bool eventFilter(QObject *obj, QEvent *event) override;
	virtual void resizeEvent(QResizeEvent *event) override;
	virtual void startDrag(Qt::DropActions supportedActions) override;
	virtual void dropEvent(QDropEvent *event) override;
	virtual void dragMoveEvent(QDragMoveEvent *event) override;
	virtual void dragLeaveEvent(QDragLeaveEvent *event) override;
	virtual void rowsInserted(const QModelIndex &parent, int start, int end) override;
	
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 3)
	virtual void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
#endif

signals:
	void scenesReordered();
};
class SceneItemWidget : public QWidget {
	Q_OBJECT
	OBSSource source;
	QListWidgetItem *item;
	QListWidget *list;
	OBSQTDisplay *display;
	QLabel *indexLabel;
	obs_weak_source_t *weakSource;
	
	QPushButton *deleteButton;

public:
	SceneItemWidget(OBSSource source, QListWidgetItem *item, QListWidget *list, QWidget *parent = nullptr);
	~SceneItemWidget();
	static void DrawScene(void *data, uint32_t cx, uint32_t cy);
	QListWidgetItem* GetItem() const { return item; }
	void UpdateIndexAndSelection(int index, bool selected);

protected:
	void mousePressEvent(QMouseEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	void enterEvent(QEnterEvent *event) override;
#else
	void enterEvent(QEvent *event) override;
#endif
	void leaveEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
};

class AddSceneItemWidget : public QWidget {
	Q_OBJECT
	QVBoxLayout *layout;
	QLabel *label;

public:
	AddSceneItemWidget(QWidget *parent = nullptr);
};

