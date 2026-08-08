/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
    Copyright (C) 2025 by Taylor Giampaolo <warchamp7@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSApp.hpp"
#include "OBSBasicSourceSelect.hpp"
#include "../widgets/OBSBasic.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <utility/ResizeSignaler.hpp>
#include <utility/ThumbnailManager.hpp>
#include <utility/ThumbnailView.hpp>

#include "qt-wrappers.hpp"

#include <obs-frontend-api.h>
#include <QList>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QGroupBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QStandardPaths>
#include <QUuid>

#include "moc_OBSBasicSourceSelect.cpp"

constexpr int kUnversionedIdRole = Qt::UserRole + 1;
constexpr int kDeprecatedRole = Qt::UserRole + 2;

constexpr QStringView kRecentTypeId = u"_recent";
constexpr int kRecentListLimit = 16;

struct AddSourceData {
	// Input data
	obs_source_t *source;
	bool visible;
	obs_transform_info *transform = nullptr;
	obs_sceneitem_crop *crop = nullptr;
	obs_blending_method *blend_method = nullptr;
	obs_blending_type *blend_mode = nullptr;
	obs_scale_type *scale_type = nullptr;
	const char *show_transition_id = nullptr;
	const char *hide_transition_id = nullptr;
	OBSData show_transition_settings;
	OBSData hide_transition_settings;
	uint32_t show_transition_duration = 300;
	uint32_t hide_transition_duration = 300;
	OBSData private_settings;

	// Return data
	obs_sceneitem_t *scene_item = nullptr;
};

namespace {
QString getDisplayNameForSourceType(QString type)
{
	if (type == "scene") {
		return QTStr("Basic.Scene");
	}

	const char *inputChar = obs_get_latest_input_type_id(type.toUtf8().constData());
	if (!inputChar) {
		return type; // Fallback to raw type string if plugin is missing
	}

	const char *displayName = obs_source_get_display_name(inputChar);

	if (!displayName) {
		return QString();
	}

	return QString::fromUtf8(displayName);
}

std::string getNewSourceName(const std::string name)
{
	std::string newName{name};
	int suffix = 1;

	for (;;) {
		OBSSourceAutoRelease existing_source = obs_get_source_by_name(newName.c_str());
		if (!existing_source) {
			break;
		}

		char nextName[256];
		std::snprintf(nextName, sizeof(nextName), "%s %d", name.data(), ++suffix);
		newName = nextName;
	}

	return newName;
}

void setupSceneItem(void *_data, obs_scene_t *scene)
{
	AddSourceData *data = (AddSourceData *)_data;
	obs_sceneitem_t *sceneitem;

	sceneitem = obs_scene_add(scene, data->source);

	if (data->transform != nullptr) {
		obs_sceneitem_set_info2(sceneitem, data->transform);
	}
	if (data->crop != nullptr) {
		obs_sceneitem_set_crop(sceneitem, data->crop);
	}
	if (data->blend_method != nullptr) {
		obs_sceneitem_set_blending_method(sceneitem, *data->blend_method);
	}
	if (data->blend_mode != nullptr) {
		obs_sceneitem_set_blending_mode(sceneitem, *data->blend_mode);
	}
	if (data->scale_type != nullptr) {
		obs_sceneitem_set_scale_filter(sceneitem, *data->scale_type);
	}

	if (data->show_transition_id && *data->show_transition_id) {
		OBSSourceAutoRelease source = obs_source_create(data->show_transition_id, data->show_transition_id,
								data->show_transition_settings, nullptr);

		if (source) {
			obs_sceneitem_set_transition(sceneitem, true, source);
		}
	}

	if (data->hide_transition_id && *data->hide_transition_id) {
		OBSSourceAutoRelease source = obs_source_create(data->hide_transition_id, data->hide_transition_id,
								data->hide_transition_settings, nullptr);

		if (source) {
			obs_sceneitem_set_transition(sceneitem, false, source);
		}
	}

	obs_sceneitem_set_transition_duration(sceneitem, true, data->show_transition_duration);
	obs_sceneitem_set_transition_duration(sceneitem, false, data->hide_transition_duration);

	obs_sceneitem_set_visible(sceneitem, data->visible);

	if (data->private_settings) {
		OBSDataAutoRelease newPrivateSettings = obs_sceneitem_get_private_settings(sceneitem);
		obs_data_apply(newPrivateSettings, data->private_settings);
	}

	data->scene_item = sceneitem;
}

std::optional<OBSSceneItem> setupExistingSource(std::string_view uuid, bool visible, bool duplicate,
						SourceCopyInfo *info = nullptr)
{
	OBSSourceAutoRelease temp = obs_get_source_by_uuid(uuid.data());
	if (!temp) {
		return std::nullopt;
	}

	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main->GetCurrentScene();
	if (!scene) {
		return std::nullopt;
	}

	if (duplicate) {
		OBSSource source = temp.Get();
		std::string new_name = getNewSourceName(obs_source_get_name(source));
		temp = obs_source_duplicate(source, new_name.c_str(), false);

		if (!source) {
			return std::nullopt;
		}
	}

	AddSourceData data;
	data.source = temp;
	data.visible = visible;

	if (info) {
		data.transform = &info->transform;
		data.crop = &info->crop;
		data.blend_method = &info->blend_method;
		data.blend_mode = &info->blend_mode;
		data.scale_type = &info->scale_type;
		data.show_transition_id = info->show_transition_id;
		data.hide_transition_id = info->hide_transition_id;
		data.show_transition_settings = std::move(info->show_transition_settings);
		data.hide_transition_settings = std::move(info->hide_transition_settings);
		data.show_transition_duration = info->show_transition_duration;
		data.hide_transition_duration = info->hide_transition_duration;
		data.private_settings = std::move(info->private_settings);
	}

	obs_enter_graphics();
	obs_scene_atomic_update(scene, setupSceneItem, &data);
	obs_leave_graphics();

	return OBSSceneItem(data.scene_item);
}

std::optional<OBSSource> setupNewSource(QWidget *parent, const char *id, const char *name)
{
	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main->GetCurrentScene();

	if (!scene) {
		return std::nullopt;
	}

	OBSSourceAutoRelease source = obs_get_source_by_name(name);
	if (source && parent) {
		OBSMessageBox::information(parent, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
		return std::nullopt;
	}

	const char *v_id = obs_get_latest_input_type_id(id);

	source = obs_source_create(v_id, name, NULL, nullptr);
	if (!source) {
		return std::nullopt;
	}

	return OBSSource(source.Get());
}
} // namespace

OBSBasicSourceSelect::OBSBasicSourceSelect(OBSBasic *parent, undo_stack &undo_s)
	: QDialog(parent),
	  ui(new Ui::OBSBasicSourceSelect),
	  undo_s(undo_s),
	  selectedTypeId(kRecentTypeId.toString()),
	  sourceButtons(new QButtonGroup(this))
{
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->setupUi(this);

	existingFlowLayout = ui->existingListFrame->flowLayout();
	existingFlowLayout->setContentsMargins(0, 0, 0, 0);
	existingFlowLayout->setSpacing(0);

	// The scroll viewport is not accessible via Designer, so we have to disable autoFillBackground here.
	// Additionally when Qt calls setWidget on a scrollArea to set the contents widget, it force sets
	// autoFillBackground to true overriding whatever is set in Designer so we have to do that here too.
	ui->existingScrollArea->viewport()->setAutoFillBackground(false);
	ui->existingScrollContents->setAutoFillBackground(false);

	ResizeSignaler *resizeSignaler = new ResizeSignaler(ui->existingScrollArea);
	ui->existingScrollArea->installEventFilter(resizeSignaler);

	connect(resizeSignaler, &ResizeSignaler::resized, this, &OBSBasicSourceSelect::updateButtonVisibility);
	connect(ui->existingScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
		&OBSBasicSourceSelect::updateButtonVisibility);
	connect(ui->existingScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this,
		&OBSBasicSourceSelect::updateButtonVisibility);

	ui->createNewFrame->setVisible(false);
	ui->deprecatedCreateLabel->setVisible(false);
	ui->deprecatedCreateLabel->setProperty("class", "text-muted");

	rebuildSourceTypeList();
	refreshSources();

	updateExistingSources(kRecentListLimit);

	signalHandlers.reserve(2);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_create", &OBSBasicSourceSelect::obsSourceCreated,
				    this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_destroy", &OBSBasicSourceSelect::obsSourceRemoved,
				    this);

	connect(ui->sourceTypeList, &QListWidget::itemDoubleClicked, this, &OBSBasicSourceSelect::createNew);
	connect(ui->sourceTypeList, &QListWidget::currentItemChanged, this, &OBSBasicSourceSelect::sourceTypeSelected);
	connect(ui->newSourceName, &QLineEdit::returnPressed, this, &OBSBasicSourceSelect::createNew);
	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(ui->addExistingButton, &QAbstractButton::clicked, this, &OBSBasicSourceSelect::addSelectedSources);
	connect(this, &OBSBasicSourceSelect::selectedItemsChanged, this, [=]() {
		ui->addExistingButton->setEnabled(selectedItems.size() > 0);
		if (selectedItems.size() > 0) {
			ui->addExistingButton->setText(QTStr("Add %1 Existing").arg(selectedItems.size()));
		} else {
			ui->addExistingButton->setText("Add Existing");
		}
	});

	App()->DisableHotkeys();
}

OBSBasicSourceSelect::~OBSBasicSourceSelect()
{
	App()->UpdateHotkeyFocusSetting();
}

void OBSBasicSourceSelect::obsSourceCreated(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<OBSBasicSourceSelect *>(data), "handleSourceCreated",
				  Qt::QueuedConnection);
}

void OBSBasicSourceSelect::obsSourceRemoved(void *data, calldata_t *params)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(params, "source"));
	const char *uuidPointer = obs_source_get_uuid(source);

	if (!uuidPointer) {
		return;
	}

	QMetaObject::invokeMethod(static_cast<OBSBasicSourceSelect *>(data), "handleSourceRemoved",
				  Qt::QueuedConnection, Q_ARG(QString, QString::fromUtf8(uuidPointer)));
}

void OBSBasicSourceSelect::sourcePaste(SourceCopyInfo &info, bool duplicate)
{
	OBSSource source = OBSGetStrongRef(info.weak_source);
	if (!source) {
		return;
	}

	std::string uuid = obs_source_get_uuid(source);

	setupExistingSource(uuid, info.visible, duplicate, &info);
}

void OBSBasicSourceSelect::showEvent(QShowEvent *)
{
	QTimer::singleShot(0, this, [this] { updateButtonVisibility(); });
}

void OBSBasicSourceSelect::updateButtonVisibility()
{
	QList<QAbstractButton *> buttons = sourceButtons->buttons();

	if (buttons.size() <= 0) {
		return;
	}

	QAbstractButton *firstButton = buttons.first();

	// Allow some room for previous/next rows to make scrolling a bit more seamless
	QRect scrollAreaRect(QPoint(0, 0), ui->existingScrollArea->size());
	scrollAreaRect.setTop(scrollAreaRect.top() - firstButton->rect().height());
	scrollAreaRect.setBottom(scrollAreaRect.bottom() + firstButton->rect().height());

	for (const auto &button : buttons) {
		SourceSelectButton *sourceButton = qobject_cast<SourceSelectButton *>(button);
		if (sourceButton) {
			QRect buttonRect = button->rect();
			buttonRect.moveTo(button->mapTo(ui->existingScrollArea, buttonRect.topLeft()));

			if (scrollAreaRect.intersects(buttonRect)) {
				sourceButton->setThumbnailEnabled(true);
			} else {
				sourceButton->setThumbnailEnabled(false);
			}
		}
	}
}

void OBSBasicSourceSelect::refreshSources()
{
	weakSources.clear();

	obs_enum_sources(enumSourcesCallback, this);

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);

	for (size_t i = 0; i < list.sources.num; ++i) {
		OBSSource source = list.sources.array[i];

		OBSWeakSourceAutoRelease weakSource = obs_source_get_weak_source(source);
		weakSources.emplace_back(weakSource);
	}
	obs_frontend_source_list_free(&list);

	emit sourcesUpdated();
}

void OBSBasicSourceSelect::updateExistingSources(int limit)
{
	QLayout *layout = ui->existingListFrame->flowLayout();

	// Clear existing buttons when switching types
	QLayoutItem *child = nullptr;
	while ((child = layout->takeAt(0)) != nullptr) {
		if (child->widget()) {
			child->widget()->deleteLater();
		}
		delete child;
	}

	if (sourceButtons) {
		sourceButtons->deleteLater();
	}

	sourceButtons = new QButtonGroup(this);
	sourceButtons->setExclusive(false);

	std::vector<obs_weak_source_t *> matchingSources{};
	std::copy_if(weakSources.begin(), weakSources.end(), std::back_inserter(matchingSources),
		     [this](obs_weak_source_t *weak) {
			     OBSSource source = OBSGetStrongRef(weak);

			     if (!source || obs_source_removed(source)) {
				     return false;
			     }

			     const char *id = obs_source_get_unversioned_id(source);
			     QString sourceTypeId = QString(id);

			     if (sourceTypeId.compare("group") == 0) {
				     return false;
			     }

			     if (selectedTypeId.compare(kRecentTypeId) == 0) {
				     // Skip listing scenes in recent sources list
				     if (sourceTypeId.compare("scene") == 0) {
					     return false;
				     }

				     return true;
			     }

			     if (selectedTypeId.compare(sourceTypeId) == 0) {
				     return true;
			     }

			     return false;
		     });

	QWidget *prevTabWidget = ui->sourceTypeList;

	auto createSourceButton = [this, &prevTabWidget](obs_weak_source_t *weak) {
		OBSSource source{OBSGetStrongRef(weak)};

		if (!source) {
			return;
		}

		SourceSelectButton *newButton = new SourceSelectButton(weak, ui->existingListFrame);
		std::string uuid = obs_source_get_uuid(source);

		existingFlowLayout->addWidget(newButton);
		sourceButtons->addButton(newButton);

		bool isSelected = false;
		if (selectedItems.size() > 0) {
			if (std::find(selectedItems.begin(), selectedItems.end(), uuid) != selectedItems.end()) {
				isSelected = true;
			}
		}

		newButton->setChecked(isSelected);

		if (!prevTabWidget) {
			setTabOrder(ui->existingListFrame, newButton);
		} else {
			setTabOrder(prevTabWidget, newButton);
		}

		prevTabWidget = newButton;
	};

	bool isReverseListOrder = selectedTypeId.compare(kRecentTypeId) == 0;
	size_t iterationLimit = limit > 0 ? std::min(static_cast<size_t>(limit), matchingSources.size())
					  : matchingSources.size();
	if (isReverseListOrder) {
		std::for_each(matchingSources.rbegin(), matchingSources.rbegin() + iterationLimit, createSourceButton);
	} else {
		std::for_each(matchingSources.begin(), matchingSources.begin() + iterationLimit, createSourceButton);
	}

	setTabOrder(prevTabWidget, ui->addExistingContainer);

	connect(sourceButtons, &QButtonGroup::buttonToggled, this, &OBSBasicSourceSelect::sourceButtonToggled);

	ui->existingListFrame->adjustSize();
}

bool OBSBasicSourceSelect::enumSourcesCallback(void *data, obs_source_t *source)
{
	if (obs_source_is_hidden(source)) {
		return true;
	}

	OBSBasicSourceSelect *window = static_cast<OBSBasicSourceSelect *>(data);

	OBSWeakSourceAutoRelease weakSource = obs_source_get_weak_source(source);
	window->weakSources.emplace_back(weakSource);

	return true;
}

void OBSBasicSourceSelect::rebuildSourceTypeList()
{
	ui->sourceTypeList->clear();

	OBSBasic *main = qobject_cast<OBSBasic *>(App()->GetMainWindow());

	auto addCustomSource = [&](const char *name, const char *unversioned_id) {
		QListWidgetItem *newItem = new QListWidgetItem(ui->sourceTypeList);
		newItem->setData(Qt::DisplayRole, name);
		newItem->setData(kUnversionedIdRole, unversioned_id);
		newItem->setData(kDeprecatedRole, false);

		QIcon icon = main->GetSourceIcon(unversioned_id);
		if (icon.isNull()) {
			icon = main->GetSourceIcon("image_source"); // fallback icon
		}
		newItem->setIcon(icon);
	};

	// --- RESTORED STANDARD OBS SOURCES ---
	const char *unversioned_type;
	const char *type;
	size_t idx = 0;

	while (obs_enum_input_types2(++idx, &type, &unversioned_type)) {
		const char *name = obs_source_get_display_name(type);
		uint32_t caps = obs_get_source_output_flags(type);

		if ((caps & OBS_SOURCE_CAP_DISABLED) != 0) {
			continue;
		}

		QListWidgetItem *newItem = new QListWidgetItem(ui->sourceTypeList);
		newItem->setData(Qt::DisplayRole, name);
		newItem->setData(kUnversionedIdRole, unversioned_type);

		if ((caps & OBS_SOURCE_DEPRECATED) != 0) {
			newItem->setData(kDeprecatedRole, true);
		} else {
			newItem->setData(kDeprecatedRole, false);

			QIcon icon;
			icon = main->GetSourceIcon(type);

			newItem->setIcon(icon);
		}
	}

	// --- CUSTOM SOURCES ---

	addCustomSource("Images & Videos", "image_source");
	addCustomSource("PowerPoint", "window_capture");
	addCustomSource("PDF", "browser_source");
	// addCustomSource("NDI\xc2\xae Input", "ndi_source");
	addCustomSource("Video Capture", "dshow_input");
	addCustomSource("IP Cameras", "ffmpeg_source");
	// addCustomSource("Desktop", "monitor_capture");
	// addCustomSource("YouTube URL", "ffmpeg_source");
	// addCustomSource("Color Source", "color_source_v3");
	// addCustomSource("Web source URL", "ffmpeg_source");
	// addCustomSource("RTMP servers", "ffmpeg_source");
	// addCustomSource("Game", "game_capture");

	QListWidgetItem *allSources = new QListWidgetItem();
	allSources->setData(Qt::DisplayRole, Str("Basic.SourceSelect.Recent"));
	allSources->setData(kUnversionedIdRole, QVariant(kRecentTypeId.toString()));
	ui->sourceTypeList->insertItem(0, allSources);

	ui->sourceTypeList->setCurrentItem(allSources);
	ui->sourceTypeList->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
}

void OBSBasicSourceSelect::sourceButtonToggled(QAbstractButton *button, bool checked)
{
	SourceSelectButton *sourceButton = dynamic_cast<SourceSelectButton *>(button);

	Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
	bool ctrlDown = (modifiers & Qt::ControlModifier);
	bool shiftDown = (modifiers & Qt::ShiftModifier);

	if (!sourceButton) {
		clearSelectedItems();
		return;
	}

	int toggledIndex = existingFlowLayout->indexOf(sourceButton);

	std::string_view toggledUuid = sourceButton->uuid();

	if (shiftDown) {
		if (!ctrlDown) {
			clearSelectedItems();
		}

		QSignalBlocker block(sourceButtons);

		int originalSelectedIndex = lastSelectedIndex;

		int start = std::min(toggledIndex, lastSelectedIndex);
		int end = std::max(toggledIndex, lastSelectedIndex);

		for (int i = start; i <= end; ++i) {
			QLayoutItem *item = existingFlowLayout->itemAt(i);
			if (!item) {
				continue;
			}

			QWidget *widget = item->widget();
			if (!widget) {
				continue;
			}

			SourceSelectButton *entry = dynamic_cast<SourceSelectButton *>(widget);
			if (entry) {
				QSignalBlocker blocker(entry);
				entry->setChecked(true);

				std::string_view uuid = entry->uuid();
				addSelectedItem(uuid.data());
			}
		}

		lastSelectedIndex = originalSelectedIndex;

	} else if (ctrlDown) {
		lastSelectedIndex = toggledIndex;

		if (checked) {
			addSelectedItem(toggledUuid.data());
		} else {
			removeSelectedItem(toggledUuid.data());
		}
	} else {
		lastSelectedIndex = toggledIndex;

		bool reselectItem = selectedItems.size() > 1;
		clearSelectedItems();
		if (checked) {
			addSelectedItem(toggledUuid.data());
		} else if (reselectItem) {
			QSignalBlocker blocker(button);
			button->setChecked(true);
			addSelectedItem(toggledUuid.data());
		}
	}
}

void OBSBasicSourceSelect::sourceDropped(QString uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toStdString().c_str());
	if (source) {
		bool visible = ui->sourceVisible->isChecked();

		addExisting(uuid.toStdString(), visible);
	}
}

void OBSBasicSourceSelect::addSelectedItem(const std::string &uuid)
{
	auto it = std::find(selectedItems.begin(), selectedItems.end(), uuid);

	if (it == selectedItems.end()) {
		selectedItems.emplace_back(uuid);
		emit selectedItemsChanged();
	}

	SourceSelectButton *button = findButtonForUuid(uuid);
	if (!button) {
		return;
	}

	lastSelectedIndex = existingFlowLayout->indexOf(button);
}

void OBSBasicSourceSelect::removeSelectedItem(const std::string &uuid)
{
	auto it = std::find(selectedItems.begin(), selectedItems.end(), uuid);
	if (it != selectedItems.end()) {
		selectedItems.erase(it);
		emit selectedItemsChanged();
	}

	SourceSelectButton *button = findButtonForUuid(uuid);
	if (!button) {
		return;
	}

	lastSelectedIndex = existingFlowLayout->indexOf(button);
}

void OBSBasicSourceSelect::clearSelectedItems()
{
	if (selectedItems.size() == 0) {
		return;
	}

	sourceButtons->blockSignals(true);
	for (const auto &uuid : selectedItems) {
		auto sourceButton = findButtonForUuid(uuid);
		if (sourceButton) {
			QSignalBlocker block(sourceButton);
			sourceButton->setChecked(false);
		}
	}
	sourceButtons->blockSignals(false);

	selectedItems.clear();
	emit selectedItemsChanged();
}

SourceSelectButton *OBSBasicSourceSelect::findButtonForUuid(const std::string &uuid)
{
	for (int i = 0; i <= existingFlowLayout->count(); ++i) {
		QLayoutItem *layoutItem = existingFlowLayout->itemAt(i);
		if (!layoutItem || !layoutItem->widget()) {
			continue;
		}

		QWidget *widget = layoutItem->widget();
		if (!widget) {
			continue;
		}

		SourceSelectButton *entry = dynamic_cast<SourceSelectButton *>(widget);
		if (entry && entry->uuid() == uuid) {
			return entry;
		}
	}

	return nullptr;
}

// Helper: create source with pre-configured settings, add to scene, open properties
static void createConfiguredSource(OBSBasicSourceSelect *dlg, const char *typeId,
				    const char *name, obs_data_t *settings, bool visible,
				    undo_stack &undo_s)
{
	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());

	OBSSourceAutoRelease existing = obs_get_source_by_name(name);
	if (existing) {
		if (dlg) {
			OBSMessageBox::information(dlg, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
		}
		return;
	}

	const char *v_id = obs_get_latest_input_type_id(typeId);
	OBSSourceAutoRelease source = obs_source_create(v_id, name, settings, nullptr);
	if (!source) {
		return;
	}

	OBSBasic *mainBasic = OBSBasic::Get();
	OBSScene scene = mainBasic->GetCurrentScene();
	if (!scene) {
		return;
	}

	AddSourceData data;
	data.source = source;
	data.visible = visible;
	obs_enter_graphics();
	obs_scene_atomic_update(scene, [](void *d, obs_scene_t *s) {
		AddSourceData *dat = (AddSourceData *)d;
		dat->scene_item = obs_scene_add(s, dat->source);
		if (dat->scene_item)
			obs_sceneitem_set_visible(dat->scene_item, dat->visible);
	}, &data);
	obs_leave_graphics();

	if (!data.scene_item) {
		return;
	}

	std::string srcUuid = obs_source_get_uuid(source);
	std::string sceneUuid = obs_source_get_uuid(mainBasic->GetCurrentSceneSource());

	auto undo = [sceneUuid](const std::string &srcData) {
		OBSBasic *m = OBSBasic::Get();
		OBSSourceAutoRelease s = obs_get_source_by_uuid(srcData.c_str());
		obs_source_remove(s);
		OBSSourceAutoRelease sc = obs_get_source_by_uuid(sceneUuid.c_str());
		m->SetCurrentScene(sc.Get(), true);
	};
	auto redo = [sceneUuid, srcUuid, visible](const std::string &) {
		OBSBasic *m = OBSBasic::Get();
		OBSSourceAutoRelease sc = obs_get_source_by_uuid(sceneUuid.c_str());
		m->SetCurrentScene(sc.Get(), true);
		OBSSourceAutoRelease s = obs_get_source_by_uuid(srcUuid.c_str());
		if (s) {
			AddSourceData d2;
			d2.source = s;
			d2.visible = visible;
			OBSScene sc2 = obs_scene_from_source(sc);
			obs_enter_graphics();
			obs_scene_atomic_update(sc2, [](void *dd, obs_scene_t *ss) {
				AddSourceData *dat = (AddSourceData *)dd;
				obs_sceneitem_t *si = obs_scene_add(ss, dat->source);
				if (si) obs_sceneitem_set_visible(si, dat->visible);
			}, &d2);
			obs_leave_graphics();
		}
	};
	undo_s.add_action(QString("Add %1").arg(QString::fromUtf8(name)), undo, redo, srcUuid, "");

	main->CreatePropertiesWindow(source);
	if (dlg) {
		dlg->close();
	}
}

void OBSBasicSourceSelect::createNew()
{
	bool visible = ui->sourceVisible->isChecked();

	if (ui->newSourceName->text().isEmpty()) {
		return;
	}

	if (selectedTypeId.compare(kRecentTypeId) == 0) {
		return;
	}

	if (selectedTypeId.compare("scene") == 0) {
		return;
	}

	std::string sourceType = selectedTypeId.toStdString();
	const char *id = sourceType.c_str();
	std::string newName = ui->newSourceName->text().toStdString();

	// ---- Custom ManyCam-style flows ----
	QString displayName = ui->sourceTypeList->currentItem()
		? ui->sourceTypeList->currentItem()->data(Qt::DisplayRole).toString()
		: QString();

	// PowerPoint: silently convert to PDF in the background and load as browser_source
	if (displayName == "PowerPoint") {
		QString file = QFileDialog::getOpenFileName(
			this, "Select PowerPoint File", "",
			"PowerPoint Files (*.pptx *.ppt *.ppsx *.odp)");
		if (file.isEmpty())
			return;

		QFileInfo fi(file);
		QString sourceName = fi.baseName().isEmpty() ? QString::fromStdString(newName) : fi.baseName();
		sourceName = QString::fromStdString(getNewSourceName(sourceName.toStdString()));

		QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
		QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
		QString pdfFile = tempDir + "/" + uuid + ".pdf";
		QString ps1File = tempDir + "/" + uuid + ".ps1";

		QString nativeFile = QDir::toNativeSeparators(file);
		QString nativePdfFile = QDir::toNativeSeparators(pdfFile);

		// Write a .ps1 script to completely avoid quoting and evaluation issues with QProcess
		QString scriptContent = QString("$ppt = New-Object -ComObject PowerPoint.Application\n"
										"$ppt.Visible = 0\n"
										"$presentation = $ppt.Presentations.Open('%1', $null, $null, 0)\n"
										"$presentation.SaveAs('%2', 32)\n"
										"$presentation.Close()\n"
										"$ppt.Quit()\n")
									.arg(nativeFile.replace("'", "''"))
									.arg(nativePdfFile.replace("'", "''"));

		QFile f(ps1File);
		if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
			f.write(scriptContent.toUtf8());
			f.close();
		}

		QStringList args;
		args << "-WindowStyle" << "Hidden" << "-ExecutionPolicy" << "Bypass" << "-File" << QDir::toNativeSeparators(ps1File);

		// Use new QProcess without parent to ensure it survives if the dialog closes
		QProcess *proc = new QProcess();
		// Capture by value [=] so we keep a copy of file, pdfFile, ps1File, sourceName, visible, undo_s
		// Pass parent() as context so the lambda fires safely on the main thread
		connect(proc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), parent(), [=](int exitCode, QProcess::ExitStatus exitStatus) {
			proc->deleteLater();
			QFile::remove(ps1File); // Cleanup the temporary script
			if (exitCode == 0 && exitStatus == QProcess::NormalExit && QFile::exists(pdfFile)) {
				const char *v_id = obs_get_latest_input_type_id("browser_source");
				if (v_id) {
					OBSDataAutoRelease s = obs_data_create();
					obs_data_set_bool(s, "is_local_file", true);
					obs_data_set_string(s, "local_file", pdfFile.toUtf8().constData());
					obs_data_set_int(s, "width", 1920);
					obs_data_set_int(s, "height", 1080);
					createConfiguredSource(nullptr, "browser_source",
						sourceName.toUtf8().constData(), s, visible, undo_s);
				}
			} else {
				// Fallback if conversion fails
				QDesktopServices::openUrl(QUrl::fromLocalFile(file));
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_string(s, "window", "PowerPoint:PPTFrameClass:POWERPNT.EXE");
				obs_data_set_int(s, "window_match_priority", 2);
				createConfiguredSource(nullptr, "window_capture",
					sourceName.toUtf8().constData(), s, visible, undo_s);
			}
		});
		proc->start("powershell.exe", args);
		return;
	}

	// PDF: open file picker, create browser source directly (fallback to window capture if no plugin)
	if (displayName == "PDF") {
		QString file = QFileDialog::getOpenFileName(
			this, "Select PDF File", "",
			"PDF Files (*.pdf)");
		if (file.isEmpty())
			return;
		QFileInfo fi(file);
		QString sourceName = fi.baseName().isEmpty() ? QString::fromStdString(newName) : fi.baseName();
		sourceName = QString::fromStdString(getNewSourceName(sourceName.toStdString()));
		
		const char *v_id = obs_get_latest_input_type_id("browser_source");
		if (v_id) {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_bool(s, "is_local_file", true);
			obs_data_set_string(s, "local_file", file.toUtf8().constData());
			obs_data_set_int(s, "width", 1920);
			obs_data_set_int(s, "height", 1080);
			createConfiguredSource(this, "browser_source",
				sourceName.toUtf8().constData(), s, visible, undo_s);
		} else {
			QDesktopServices::openUrl(QUrl::fromLocalFile(file));
			QTimer::singleShot(2500, this, [=]() {
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_string(s, "window", "Chrome"); // generic match
				createConfiguredSource(this, "window_capture",
					sourceName.toUtf8().constData(), s, visible, undo_s);
			});
		}
		return;
	}

	// Images & Videos: open file picker, create media/image source
	if (displayName == "Images & Videos") {
		QString file = QFileDialog::getOpenFileName(
			this, "Select Image or Video File", "",
			"Media Files (*.png *.jpg *.jpeg *.bmp *.gif *.mp4 *.mov *.mkv *.avi *.webm *.wmv)");
		if (file.isEmpty())
			return;
		QFileInfo fi(file);
		QString sourceName = fi.baseName().isEmpty() ? QString::fromStdString(newName) : fi.baseName();
		sourceName = QString::fromStdString(getNewSourceName(sourceName.toStdString()));
		// Use image_source for images, ffmpeg_source for video
		QStringList videoExts = {"mp4","mov","mkv","avi","webm","wmv"};
		bool isVideo = videoExts.contains(fi.suffix().toLower());
		const char *srcType = isVideo ? "ffmpeg_source" : "image_source";
		OBSDataAutoRelease s = obs_data_create();
		if (isVideo)
			obs_data_set_string(s, "local_file", file.toUtf8().constData());
		else
			obs_data_set_string(s, "file", file.toUtf8().constData());
		createConfiguredSource(this, srcType,
			sourceName.toUtf8().constData(), s, visible, undo_s);
		return;
	}

	// YouTube URL / Web source URL: show URL input dialog, create browser source (fallback to ffmpeg)
	if (displayName == "YouTube URL" || displayName == "Web source URL") {
		bool ok = false;
		QString url = QInputDialog::getText(
			this,
			displayName == "YouTube URL" ? "YouTube / Stream URL" : "Web Source URL",
			"Enter URL (YouTube, RTMP, HLS, DASH, direct video link):",
			QLineEdit::Normal, "", &ok);
		if (!ok || url.trimmed().isEmpty())
			return;
		
		QString parsedUrl = url.trimmed();
		QRegularExpression ytRegex("(?:youtube\\.com\\/watch\\?v=|youtu\\.be\\/)([a-zA-Z0-9_-]{11})");
		QRegularExpressionMatch match = ytRegex.match(parsedUrl);
		if (match.hasMatch()) {
			QString videoId = match.captured(1);
			parsedUrl = QString("https://www.youtube.com/embed/%1?autoplay=1").arg(videoId);
		}

		QString sourceName = QString::fromStdString(getNewSourceName(newName));
		
		const char *v_id = obs_get_latest_input_type_id("browser_source");
		if (v_id) {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_string(s, "url", parsedUrl.toUtf8().constData());
			obs_data_set_bool(s, "is_local_file", false);
			obs_data_set_int(s, "width", 1920);
			obs_data_set_int(s, "height", 1080);
			createConfiguredSource(this, "browser_source",
				sourceName.toUtf8().constData(), s, visible, undo_s);
		} else {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_string(s, "input", parsedUrl.toUtf8().constData());
			obs_data_set_bool(s, "is_local_file", false);
			createConfiguredSource(this, "ffmpeg_source",
				sourceName.toUtf8().constData(), s, visible, undo_s);
		}
		return;
	}

	// IP Cameras / RTMP servers: show URL input dialog
	if (displayName == "IP Cameras" || displayName == "RTMP servers") {
		bool ok = false;
		QString url;
		std::string finalName = newName;
		
		if (displayName == "IP Cameras") {
			QDialog dlg(this);
			dlg.setWindowTitle("IP Camera Settings");
			dlg.setMinimumWidth(450);
			QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
			
			QFormLayout *formLayout = new QFormLayout();
			mainLayout->addLayout(formLayout);
			
			QLineEdit *nameEdit = new QLineEdit(&dlg);
			nameEdit->setText(QString::fromStdString(getNewSourceName(finalName)));
			formLayout->addRow("Camera Name:", nameEdit);
			
			QLineEdit *urlEdit = new QLineEdit(&dlg);
			urlEdit->setPlaceholderText("rtsp://192.168.1.108:554/stream1");
			formLayout->addRow("URL:", urlEdit);
			
			QCheckBox *authCheck = new QCheckBox("Access required", &dlg);
			mainLayout->addWidget(authCheck);
			
			QGroupBox *authGroup = new QGroupBox(&dlg);
			QFormLayout *authLayout = new QFormLayout(authGroup);
			authGroup->setStyleSheet("QGroupBox { border: none; margin-top: 0px; }");
			
			QLineEdit *loginEdit = new QLineEdit(authGroup);
			authLayout->addRow("Login:", loginEdit);
			
			QLineEdit *passEdit = new QLineEdit(authGroup);
			passEdit->setEchoMode(QLineEdit::Password);
			authLayout->addRow("Password:", passEdit);
			
			authGroup->setLayout(authLayout);
			authGroup->setVisible(false);
			
			QObject::connect(authCheck, &QCheckBox::toggled, authGroup, &QGroupBox::setVisible);
			mainLayout->addWidget(authGroup);
			
			QObject::connect(urlEdit, &QLineEdit::textEdited, urlEdit, [&dlg, urlEdit, authCheck, loginEdit, passEdit](const QString &text) {
				int doubleSlashIdx = text.indexOf("://");
				if (doubleSlashIdx != -1) {
					int atIdx = text.indexOf("@", doubleSlashIdx + 3);
					if (atIdx != -1) {
						int colonIdx = text.indexOf(":", doubleSlashIdx + 3);
						if (colonIdx != -1 && colonIdx < atIdx) {
							QString login = text.mid(doubleSlashIdx + 3, colonIdx - (doubleSlashIdx + 3));
							QString pass = text.mid(colonIdx + 1, atIdx - (colonIdx + 1));
							
							loginEdit->setText(QUrl::fromPercentEncoding(login.toUtf8()));
							passEdit->setText(QUrl::fromPercentEncoding(pass.toUtf8()));
							authCheck->setChecked(true);
							
							QString newText = text.left(doubleSlashIdx + 3) + text.mid(atIdx + 1);
							urlEdit->setText(newText);
						}
					}
				}
			});
			
			QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
			QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
			QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
			mainLayout->addWidget(buttonBox);
			
			if (dlg.exec() == QDialog::Accepted) {
				QString inputUrl = urlEdit->text().trimmed();
				if (!inputUrl.isEmpty()) {
					if (authCheck->isChecked() && !loginEdit->text().isEmpty()) {
						int doubleSlashIdx = inputUrl.indexOf("://");
						if (doubleSlashIdx != -1) {
							QString protocol = inputUrl.left(doubleSlashIdx + 3);
							QString rest = inputUrl.mid(doubleSlashIdx + 3);
							QString login = QUrl::toPercentEncoding(loginEdit->text());
							QString pass = QUrl::toPercentEncoding(passEdit->text());
							url = protocol + login + ":" + pass + "@" + rest;
						} else {
							url = inputUrl;
						}
					} else {
						url = inputUrl;
					}
					if (!nameEdit->text().trimmed().isEmpty()) {
						finalName = nameEdit->text().trimmed().toStdString();
					}
					ok = true;
					
					config_t *config = OBSBasic::Get()->Config();
					if (config) {
						const char *jsonStr = config_get_string(config, "IPCameras", "Cameras");
						QJsonArray arr;
						if (jsonStr && *jsonStr) {
							QJsonDocument doc = QJsonDocument::fromJson(QByteArray(jsonStr));
							if (doc.isArray()) arr = doc.array();
						}
						for (int i = 0; i < arr.size(); ++i) {
							if (arr[i].toObject()["name"].toString() == QString::fromStdString(finalName)) {
								arr.removeAt(i);
								break;
							}
						}
						QJsonObject obj;
						obj["name"] = QString::fromStdString(finalName);
						obj["url"] = url;
						arr.append(obj);
						QJsonDocument newDoc(arr);
						config_set_string(config, "IPCameras", "Cameras", newDoc.toJson(QJsonDocument::Compact).constData());
						config_save_safe(config, "tmp", nullptr);
					}
				}
			}
		} else {
			QString label = "Enter RTMP/stream server URL:";
			url = QInputDialog::getText(
				this, displayName, label, QLineEdit::Normal, "", &ok);
		}
		
		if (!ok || url.trimmed().isEmpty())
			return;
		QString sourceName = QString::fromStdString(getNewSourceName(finalName));
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_string(s, "input", url.trimmed().toUtf8().constData());
		obs_data_set_bool(s, "is_local_file", false);
		createConfiguredSource(this, "ffmpeg_source",
			sourceName.toUtf8().constData(), s, visible, undo_s);
		
		OBSSourceAutoRelease newSrc = obs_get_source_by_name(sourceName.toUtf8().constData());
		if (newSrc) {
			obs_frontend_source_list scenes = {};
			obs_frontend_get_scenes(&scenes);
			OBSBasic *mainBasic = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
			obs_source_t *currentSceneSrc = mainBasic->GetCurrentSceneSource();
			
			for (size_t k = 0; k < scenes.sources.num; k++) {
				obs_source_t *scene_source = scenes.sources.array[k];
				if (scene_source != currentSceneSrc) {
					obs_scene_t *scene = obs_scene_from_source(scene_source);
					if (scene) obs_scene_add(scene, newSrc);
				}
			}
			obs_frontend_source_list_free(&scenes);
		}
		
		return;
	}

	// ---- Default: standard OBS flow ----
	std::optional<OBSSource> createResult = setupNewSource(this, id, newName.c_str());
	if (!createResult.has_value()) {
		return;
	}

	OBSSource newSource = createResult.value();
	if (strcmp(obs_source_get_id(newSource), "group") == 0) {
		return;
	}

	std::optional<OBSSceneItem> addResult = setupExistingSource(obs_source_get_uuid(newSource), visible, false);
	if (!addResult.has_value()) {
		return;
	}

	OBSSceneItem item = addResult.value();

	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	std::string sceneUuid = obs_source_get_uuid(main->GetCurrentSceneSource());
	auto undo = [sceneUuid](const std::string &data) {
		OBSBasic *main = OBSBasic::Get();

		OBSSourceAutoRelease source = obs_get_source_by_uuid(data.c_str());
		obs_source_remove(source);

		OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.c_str());
		main->SetCurrentScene(sceneSource.Get(), true);
	};
	OBSDataAutoRelease wrapper = obs_data_create();
	obs_data_set_string(wrapper, "id", id);
	obs_data_set_int(wrapper, "item_id", obs_sceneitem_get_id(item));
	obs_data_set_string(wrapper, "name", ui->newSourceName->text().toUtf8().constData());
	obs_data_set_bool(wrapper, "visible", visible);

	auto redo = [sceneUuid](const std::string &data) {
		OBSBasic *main = OBSBasic::Get();

		OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.c_str());
		main->SetCurrentScene(sceneSource.Get(), true);

		OBSDataAutoRelease dat = obs_data_create_from_json(data.c_str());

		std::optional<OBSSource> createResult =
			setupNewSource(NULL, obs_data_get_string(dat, "id"), obs_data_get_string(dat, "name"));
		if (!createResult.has_value()) {
			return;
		}

		OBSSource source = createResult.value();

		std::optional<OBSSceneItem> addResult =
			setupExistingSource(obs_source_get_uuid(source), obs_data_get_bool(dat, "visible"), false);
		if (!addResult.has_value()) {
			return;
		}

		OBSSceneItem item = addResult.value();

		obs_sceneitem_set_id(item, static_cast<int64_t>(obs_data_get_int(dat, "item_id")));
	};
	undo_s.add_action(QTStr("Undo.Add").arg(ui->newSourceName->text()), undo, redo,
			  std::string(obs_source_get_uuid(newSource)), std::string(obs_data_get_json(wrapper)));

	main->CreatePropertiesWindow(newSource);

	close();
}

void OBSBasicSourceSelect::addExisting(const std::string &uuid, bool visible)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());
	if (!source) {
		return;
	}

	QString name = obs_source_get_name(source);
	setupExistingSource(uuid, visible, false);

	OBSBasic *main = OBSBasic::Get();
	const char *sceneUuidPtr = obs_source_get_uuid(main->GetCurrentSceneSource());
	if (!sceneUuidPtr) {
		return;
	}

	std::string sceneUuid{sceneUuidPtr};

	auto undo = [sceneUuid, main](const std::string &) {
		OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.c_str());
		main->SetCurrentScene(sceneSource.Get(), true);

		OBSScene scene = obs_scene_from_source(sceneSource);
		OBSSceneItem item;
		auto cb = [](obs_scene_t *, obs_sceneitem_t *sceneitem, void *data) {
			OBSSceneItem &last = *static_cast<OBSSceneItem *>(data);
			last = sceneitem;
			return true;
		};
		obs_scene_enum_items(scene, cb, &item);

		obs_sceneitem_remove(item);
	};

	auto redo = [sceneUuid, main, uuid, visible](const std::string &) {
		OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.c_str());
		main->SetCurrentScene(sceneSource.Get(), true);

		setupExistingSource(uuid, visible, false);
	};

	undo_s.add_action(QTStr("Undo.Add").arg(name), undo, redo, "", "");
}

void OBSBasicSourceSelect::on_createNewSource_clicked(bool)
{
	createNew();
}

void OBSBasicSourceSelect::addSelectedSources()
{
	if (selectedItems.size() == 0) {
		return;
	}

	bool visible = ui->sourceVisible->isChecked();

	for (const auto &uuid : selectedItems) {
		addExisting(uuid, visible);
	}
	close();
}

void OBSBasicSourceSelect::handleSourceCreated()
{
	refreshSources();

	if (selectedTypeId.compare(kRecentTypeId) == 0) {
		updateExistingSources(kRecentListLimit);
	} else {
		updateExistingSources();
	}
}

void OBSBasicSourceSelect::handleSourceRemoved(QString uuid)
{
	refreshSources();

	removeSelectedItem(uuid.toStdString());

	if (selectedTypeId.compare(kRecentTypeId) == 0) {
		updateExistingSources(kRecentListLimit);
	} else {
		updateExistingSources();
	}
}

void OBSBasicSourceSelect::sourceTypeSelected(QListWidgetItem *current, QListWidgetItem *)
{
	clearSelectedItems();

	QVariant unversionedIdData = current->data(kUnversionedIdRole);
	QVariant deprecatedData = current->data(kDeprecatedRole);

	if (unversionedIdData.toString().compare(kRecentTypeId) == 0) {
		selectedTypeId = kRecentTypeId.toString();
		ui->createNewFrame->setVisible(false);
		updateExistingSources(kRecentListLimit);
		return;
	}

	QString type = unversionedIdData.toString();
	if (type.compare(selectedTypeId) == 0) {
		return;
	}

	ui->createNewFrame->setVisible(true);

	bool isDeprecatedType = deprecatedData.toBool();
	ui->deprecatedCreateLabel->setVisible(isDeprecatedType);

	selectedTypeId = type;

	QString placeHolderText{getDisplayNameForSourceType(selectedTypeId)};

	QString text{placeHolderText};
	int i = 2;
	OBSSourceAutoRelease source = nullptr;
	while ((source = obs_get_source_by_name(QT_TO_UTF8(text)))) {
		text = QString("%1 %2").arg(placeHolderText).arg(i++);
	}

	ui->newSourceName->setText(text);

	updateExistingSources();

	if (existingFlowLayout->count() == 0) {
		QLabel *noExisting = new QLabel();
		noExisting->setText(
			QTStr("Basic.SourceSelect.NoExisting").arg(getDisplayNameForSourceType(selectedTypeId)));
		noExisting->setProperty("class", "text-muted");
		existingFlowLayout->addWidget(noExisting);
	}
}

void OBSBasicSourceSelect::setInitialType(const QString &typeName)
{
	for (int i = 0; i < ui->sourceTypeList->count(); i++) {
		QListWidgetItem *item = ui->sourceTypeList->item(i);
		if (item->data(Qt::DisplayRole).toString() == typeName) {
			ui->sourceTypeList->setCurrentItem(item);
			break;
		}
	}
}
