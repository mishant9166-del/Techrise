#include "CustomStreamDialog.hpp"
#include "ui_CustomStreamDialog.h"
#include <QPainter>
#include <QPainterPath>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <obs-frontend-api.h>
#include "../widgets/OBSBasic.hpp"

// ToggleSwitch widget
class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
public:
    explicit ToggleSwitch(QWidget *parent = nullptr) : QWidget(parent), m_checked(false) {
        setFixedSize(40, 20);
        setCursor(Qt::PointingHandCursor);
    }
    bool isChecked() const { return m_checked; }
    void setChecked(bool checked) {
        if (m_checked != checked) {
            m_checked = checked;
            emit toggled(m_checked);
            update();
        }
    }
signals:
    void toggled(bool checked);
protected:
    void mouseReleaseEvent(QMouseEvent *) override {
        setChecked(!m_checked);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_checked) {
            p.setBrush(QColor("#00a8ff"));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(0, 0, 40, 20, 10, 10);
            p.setBrush(Qt::white);
            p.drawEllipse(22, 2, 16, 16);
        } else {
            p.setBrush(underMouse() ? QColor("#d0d0d0") : QColor("#dcdcdc"));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(0, 0, 40, 20, 10, 10);
            p.setBrush(Qt::white);
            p.drawEllipse(2, 2, 16, 16);
        }
    }
private:
    bool m_checked;
};

CustomStreamDialog::CustomStreamDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::CustomStreamDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    // Attempt to restore all sessions on boot
    PlatformSessionManager::Get()->restoreAllSessions();

    SetupChannels();

    // Load Settings
    if (OBSBasic* main = OBSBasic::Get()) {
        config_t* config = main->Config();
        if (config) {
            int vbitrate = config_get_int(config, "SimpleOutput", "VBitrate");
            ui->videoBitrateEdit->setText(QString::number(vbitrate));
            
            int abitrate = config_get_uint(config, "SimpleOutput", "ABitrate");
            ui->audioBitrateCombo->setCurrentText(QString::number(abitrate));
            
            const char* encoder = config_get_string(config, "SimpleOutput", "StreamEncoder");
            if (encoder) {
                if (strcmp(encoder, "qsv") == 0) ui->encoderCombo->setCurrentIndex(0);
                else if (strcmp(encoder, "x264") == 0) ui->encoderCombo->setCurrentIndex(1);
            }
            
            const char* preset = config_get_string(config, "SimpleOutput", "Preset");
            if (preset) {
                if (strcmp(preset, "quality") == 0) ui->presetCombo->setCurrentIndex(0);
                else if (strcmp(preset, "performance") == 0) ui->presetCombo->setCurrentIndex(1);
            }
        }
    }

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &CustomStreamDialog::updateUIStatus);
    updateTimer->start(1000);
}

CustomStreamDialog::~CustomStreamDialog()
{
}

void CustomStreamDialog::on_navStreamBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(0); 
    ui->navStreamBtn->setChecked(true);
    ui->navSettingsBtn->setChecked(false);
}
void CustomStreamDialog::on_navSettingsBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(1); 
    ui->navStreamBtn->setChecked(false);
    ui->navSettingsBtn->setChecked(true);
}
void CustomStreamDialog::on_subtitleSettingsBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(1); 
    ui->navStreamBtn->setChecked(false);
    ui->navSettingsBtn->setChecked(true);
}
void CustomStreamDialog::on_addCustomServerBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(2); 
    ui->navStreamBtn->setChecked(false);
    ui->navSettingsBtn->setChecked(false);
}
void CustomStreamDialog::on_saveSettingsBtn_clicked() { 
    if (OBSBasic* main = OBSBasic::Get()) {
        config_t* config = main->Config();
        if (config) {
            QString encoder = ui->encoderCombo->currentText();
            if (encoder.contains("Quick Sync")) {
                config_set_string(config, "SimpleOutput", "StreamEncoder", "qsv");
            } else {
                config_set_string(config, "SimpleOutput", "StreamEncoder", "x264");
            }

            QString vbitrate = ui->videoBitrateEdit->text();
            if (!vbitrate.isEmpty()) {
                config_set_int(config, "SimpleOutput", "VBitrate", vbitrate.toInt());
            }

            QString preset = ui->presetCombo->currentText();
            if (preset == "Quality") {
                config_set_string(config, "SimpleOutput", "Preset", "quality");
            } else {
                config_set_string(config, "SimpleOutput", "Preset", "performance");
            }

            QString abitrate = ui->audioBitrateCombo->currentText();
            if (!abitrate.isEmpty()) {
                config_set_uint(config, "SimpleOutput", "ABitrate", abitrate.toUInt());
            }

            main->ResetOutputs();
        }
    }

    ui->stackedWidget->setCurrentIndex(0); 
    ui->navStreamBtn->setChecked(true);
    ui->navSettingsBtn->setChecked(false);
}
void CustomStreamDialog::on_cancelSettingsBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(0); 
    ui->navStreamBtn->setChecked(true);
    ui->navSettingsBtn->setChecked(false);
}
void CustomStreamDialog::on_createServerBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(0); 
    ui->navStreamBtn->setChecked(true);
    ui->navSettingsBtn->setChecked(false);
}
void CustomStreamDialog::on_cancelServerBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(0); 
    ui->navStreamBtn->setChecked(true);
    ui->navSettingsBtn->setChecked(false);
}
void CustomStreamDialog::on_backServerBtn_clicked() { 
    ui->stackedWidget->setCurrentIndex(0); 
    ui->navStreamBtn->setChecked(true);
    ui->navSettingsBtn->setChecked(false);
}

void CustomStreamDialog::SetupChannels()
{
    PlatformSessionManager* mgr = PlatformSessionManager::Get();
    
    AddChannelCard("YouTube", mgr->getYouTube());
    AddChannelCard("Facebook", mgr->getFacebook());
    AddChannelCard("Twitch", mgr->getTwitch());
    
    ui->channelsLayout->addStretch();
}

void CustomStreamDialog::AddChannelCard(const QString &name, OAuthClient* client)
{
    QFrame *card = new QFrame(ui->scrollAreaWidgetContents);
    card->setProperty("class", "card");
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet("QFrame.card { background-color: white; border: 1px solid #e0e0e0; border-radius: 8px; margin-bottom: 10px; }");
    
    card->setObjectName(name + "_card");

    QVBoxLayout *mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(15, 10, 15, 10);
    
    QWidget *headerWidget = new QWidget(card);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    
    QString svgData;
    if (name == "Facebook") {
        svgData = "<svg viewBox='0 0 24 24' width='24' height='24'><path fill='#1877f2' d='M24 12.073c0-6.627-5.373-12-12-12s-12 5.373-12 12c0 5.99 4.388 10.954 10.125 11.854v-8.385H7.078v-3.47h3.047V9.43c0-3.007 1.792-4.669 4.533-4.669 1.312 0 2.686.235 2.686.235v2.953H15.83c-1.491 0-1.956.925-1.956 1.874v2.25h3.328l-.532 3.47h-2.796v8.385C19.612 23.027 24 18.062 24 12.073z'/></svg>";
    } else if (name == "YouTube") {
        svgData = "<svg viewBox='0 0 24 24' width='24' height='24'><path fill='#ff0000' d='M23.498 6.186a3.016 3.016 0 0 0-2.122-2.136C19.505 3.545 12 3.545 12 3.545s-7.505 0-9.377.505A3.017 3.017 0 0 0 .502 6.186C0 8.07 0 12 0 12s0 3.93.502 5.814a3.016 3.016 0 0 0 2.122 2.136c1.871.505 9.376.505 9.376.505s7.505 0 9.377-.505a3.015 3.015 0 0 0 2.122-2.136C24 15.93 24 12 24 12s0-3.93-.502-5.814zM9.545 15.568V8.432L15.818 12l-6.273 3.568z'/></svg>";
    } else if (name == "Twitch") {
        svgData = "<svg viewBox='0 0 24 24' width='24' height='24'><path fill='#9146FF' d='M2.149 0L.537 4.119v16.836h5.731V24h3.224l3.045-3.045h4.657l6.269-6.269V0H2.149zm19.164 13.612l-4.298 4.298h-4.657l-3.045 3.045v-3.045H4.119V2.149h17.194v11.463zm-3.582-7.343v5.731h-2.149V6.269h2.149zm-5.731 0v5.731h-2.149V6.269h2.149z'/></svg>";
    }

    if (!svgData.isEmpty()) {
        QLabel *iconLabel = new QLabel(headerWidget);
        iconLabel->setStyleSheet("border: none; background: transparent;");
        QPixmap pixmap;
        pixmap.loadFromData(svgData.toUtf8(), "SVG");
        iconLabel->setPixmap(pixmap.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        headerLayout->addWidget(iconLabel);
    }
    
    QLabel *nameLabel = new QLabel(name, headerWidget);
    nameLabel->setStyleSheet("font-size: 14px; color: #333; border: none; background: transparent; font-weight: bold;");
    headerLayout->addWidget(nameLabel);

    QLabel *statusLabel = new QLabel("", headerWidget);
    statusLabel->setObjectName(name + "_statusLabel");
    statusLabel->setStyleSheet("font-size: 13px; color: #999; border: none; background: transparent; font-style: italic; margin-left: 10px;");
    headerLayout->addWidget(statusLabel);
    statusLabel->hide();
    
    headerLayout->addStretch();
    
    QPushButton* loginBtn = new QPushButton("Login", headerWidget);
    loginBtn->setCursor(Qt::PointingHandCursor);
    loginBtn->setStyleSheet("QPushButton { color: #00a8ff; font-weight: bold; border: none; background: transparent; } QPushButton:hover { color: #0077ff; }");
    
    QPushButton* logoutBtn = new QPushButton("Logout", headerWidget);
    logoutBtn->setCursor(Qt::PointingHandCursor);
    logoutBtn->setStyleSheet("QPushButton { color: #00a8ff; font-weight: bold; border: none; background: transparent; } QPushButton:hover { color: #0077ff; }");
    
    QPushButton* editBtn = new QPushButton("", headerWidget);
    QPixmap pencilPixmap;
    QString pencilSvg = "<svg viewBox='0 0 24 24' width='16' height='16'><path fill='#888' d='M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z'/></svg>";
    pencilPixmap.loadFromData(pencilSvg.toUtf8(), "SVG");
    editBtn->setIcon(QIcon(pencilPixmap));
    editBtn->setCursor(Qt::PointingHandCursor);
    editBtn->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: #eee; border-radius: 4px; }");

    
    ToggleSwitch* toggle = new ToggleSwitch(headerWidget);
    toggle->setObjectName(name + "_toggle");
    
    headerLayout->addWidget(loginBtn);
    headerLayout->addWidget(logoutBtn);
    headerLayout->addWidget(editBtn);
    headerLayout->addWidget(toggle);
    
    // Settings Widget
    QWidget *settingsWidget = new QWidget(card);
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsWidget);
    settingsLayout->setContentsMargins(0, 10, 0, 0);
    
    QLabel* titleLbl = new QLabel("Stream title", settingsWidget);
    titleLbl->setStyleSheet("color: #666; font-size: 12px; border: none;");
    QLineEdit* titleEdit = new QLineEdit(settingsWidget);
    titleEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    titleEdit->setPlaceholderText("Enter stream title");
    titleEdit->setStyleSheet("QLineEdit { background: white; color: black; border: 1px solid #ccc; border-radius: 4px; padding: 5px; }");
    
    QLabel* descLbl = new QLabel("Description", settingsWidget);
    descLbl->setStyleSheet("color: #666; font-size: 12px; border: none;");
    QTextEdit* descEdit = new QTextEdit(settingsWidget);
    descEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    descEdit->setPlaceholderText("Tell more about your stream...");
    descEdit->setFixedHeight(60);
    descEdit->setStyleSheet("QTextEdit { background: white; color: black; border: 1px solid #ccc; border-radius: 4px; padding: 5px; }");
    
    QHBoxLayout* combosLayout = new QHBoxLayout();
    QVBoxLayout* destLayout = new QVBoxLayout();
    QLabel* destLbl = new QLabel("Category/Destination", settingsWidget);
    destLbl->setStyleSheet("color: #666; font-size: 12px; border: none;");
    QComboBox* destCombo = new QComboBox(settingsWidget);
    destCombo->setStyleSheet("QComboBox { background: white; color: black; border: 1px solid #ccc; border-radius: 4px; padding: 5px; } QComboBox QAbstractItemView { background: white; color: black; }");
    destCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    destCombo->addItem("Gaming");
    destCombo->addItem("Just Chatting");
    destCombo->addItem("Entertainment");
    destCombo->addItem("Music");
    destCombo->addItem("Sports");
    destCombo->addItem("News & Politics");
    destCombo->addItem("Education");
    destCombo->addItem("Science & Technology");
    destLayout->addWidget(destLbl);
    destLayout->addWidget(destCombo);
    
    QVBoxLayout* privLayout = new QVBoxLayout();
    QLabel* privLbl = new QLabel("Privacy", settingsWidget);
    privLbl->setStyleSheet("color: #666; font-size: 12px; border: none;");
    QComboBox* privCombo = new QComboBox(settingsWidget);
    privCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    privCombo->setStyleSheet("QComboBox { background: white; color: black; border: 1px solid #ccc; border-radius: 4px; padding: 5px; } QComboBox QAbstractItemView { background: white; color: black; }");
    privCombo->addItem("Public");
    privCombo->addItem("Unlisted");
    privCombo->addItem("Private");
    privLayout->addWidget(privLbl);
    privLayout->addWidget(privCombo);
    
    combosLayout->addLayout(destLayout);
    combosLayout->addLayout(privLayout);
    
    settingsLayout->addWidget(titleLbl);
    settingsLayout->addWidget(titleEdit);
    settingsLayout->addWidget(descLbl);
    settingsLayout->addWidget(descEdit);
    settingsLayout->addLayout(combosLayout);
    
    mainLayout->addWidget(headerWidget);
    mainLayout->addWidget(settingsWidget);
    settingsWidget->hide();
    ui->channelsLayout->addWidget(card);

    connect(editBtn, &QPushButton::clicked, this, [=]() {
        settingsWidget->setVisible(!settingsWidget->isVisible());
    });

    // Network manager for downloading avatars
    QNetworkAccessManager* avatarManager = new QNetworkAccessManager(this);

    // Update UI Lambda
    auto updateUI = [=]() {
        PlatformSession session = client->getSession();
        
        // Prevent signal loops
        titleEdit->blockSignals(true);
        descEdit->blockSignals(true);
        privCombo->blockSignals(true);
        destCombo->blockSignals(true);
        toggle->blockSignals(true);
        
        if (session.loggedIn) {
            loginBtn->hide();
            logoutBtn->show();
            editBtn->show();
            toggle->show();
            
            toggle->setChecked(session.streamEnabled);
            
            if (titleEdit->text() != session.streamTitle) {
                titleEdit->setText(session.streamTitle);
            }
            if (descEdit->toPlainText() != session.streamDescription) {
                descEdit->setText(session.streamDescription);
            }
            QString targetPrivacy = session.streamPrivacy.isEmpty() ? "Public" : session.streamPrivacy;
            if (privCombo->currentText() != targetPrivacy) {
                privCombo->setCurrentText(targetPrivacy);
            }
            QString targetCategory = session.streamCategory.isEmpty() ? "Gaming" : session.streamCategory;
            if (destCombo->currentText() != targetCategory) {
                destCombo->setCurrentText(targetCategory);
            }
        } else {
            loginBtn->show();
            logoutBtn->hide();
            editBtn->hide();
            toggle->hide();
            settingsWidget->hide();
        }
        
        titleEdit->blockSignals(false);
        descEdit->blockSignals(false);
        privCombo->blockSignals(false);
        destCombo->blockSignals(false);
        toggle->blockSignals(false);
    };

    connect(client, &OAuthClient::sessionUpdated, this, updateUI);
    updateUI(); // Init
    
    // Connections
    connect(loginBtn, &QPushButton::clicked, client, &OAuthClient::startLogin);
    connect(logoutBtn, &QPushButton::clicked, client, &OAuthClient::logout);
    
    connect(toggle, &ToggleSwitch::toggled, this, [=](bool checked) {
        PlatformSession s = client->getSession();
        s.streamEnabled = checked;
        
        s.streamTitle = titleEdit->text().isEmpty() ? "-" : titleEdit->text();
        s.streamDescription = descEdit->toPlainText().isEmpty() ? "-" : descEdit->toPlainText();
        s.streamCategory = destCombo->currentText();
        s.streamPrivacy = privCombo->currentText();
        
        client->updateSession(s);
        client->saveTokens();
        
        if (checked) {
            PlatformSessionManager::Get()->startStream(client);
        } else {
            PlatformSessionManager::Get()->stopStream(client);
        }
    });
    
    connect(titleEdit, &QLineEdit::textChanged, this, [=](const QString& text) {
        PlatformSession s = client->getSession();
        s.streamTitle = text;
        client->updateSession(s);
    });
    
    connect(descEdit, &QTextEdit::textChanged, this, [=]() {
        PlatformSession s = client->getSession();
        s.streamDescription = descEdit->toPlainText();
        client->updateSession(s);
    });
    
    connect(privCombo, &QComboBox::currentTextChanged, this, [=](const QString& text) {
        PlatformSession s = client->getSession();
        s.streamPrivacy = text;
        client->updateSession(s);
    });
    
    connect(destCombo, &QComboBox::currentTextChanged, this, [=](const QString& text) {
        PlatformSession s = client->getSession();
        s.streamCategory = text;
        client->updateSession(s);
    });
}

void CustomStreamDialog::updateUIStatus()
{
    PlatformSessionManager* mgr = PlatformSessionManager::Get();
    
    auto updateCard = [&](const QString& platName, OAuthClient* client) {
        if (!client) return;
        
        QLabel* statusLbl = this->findChild<QLabel*>(platName + "_statusLabel");
        QFrame* card = this->findChild<QFrame*>(platName + "_card");
        QWidget* toggle = this->findChild<QWidget*>(platName + "_toggle");
        
        if (!statusLbl || !card || !toggle) return;
        int state = mgr->getPlatformState(platName);
        if (state > 0) {
            if (state == 1) {
                statusLbl->setText("Connecting...");
                statusLbl->setStyleSheet("font-size: 13px; color: #ffa500; border: none; background: transparent; font-style: italic; margin-left: 10px;");
            } else {
                statusLbl->setText("Connected");
                statusLbl->setStyleSheet("font-size: 13px; color: #4caf50; border: none; background: transparent; font-weight: bold; margin-left: 10px;");
            }
            statusLbl->show();
            toggle->setToolTip("Stop stream");
            ToggleSwitch* tsw = qobject_cast<ToggleSwitch*>(toggle);
            if (tsw) { tsw->blockSignals(true); tsw->setChecked(true); tsw->blockSignals(false); }
            card->setStyleSheet("QFrame.card { background-color: white; border: 1px solid #00a8ff; border-radius: 8px; margin-bottom: 10px; }");
        } else {
            statusLbl->setText("");
            statusLbl->hide();
            toggle->setToolTip("");
            ToggleSwitch* tsw = qobject_cast<ToggleSwitch*>(toggle);
            if (tsw) { tsw->blockSignals(true); tsw->setChecked(false); tsw->blockSignals(false); }
            card->setStyleSheet("QFrame.card { background-color: white; border: 1px solid #e0e0e0; border-radius: 8px; margin-bottom: 10px; }");
        }
    };
    
    updateCard("YouTube", mgr->getYouTube());
    updateCard("Facebook", mgr->getFacebook());
    updateCard("Twitch", mgr->getTwitch());
}
#include "CustomStreamDialog.moc"
