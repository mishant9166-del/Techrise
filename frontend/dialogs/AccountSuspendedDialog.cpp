#include "AccountSuspendedDialog.hpp"

#include <OBSApp.hpp>
#include <QDesktopServices>
#include <QTimer>
#include "LicenseManager.hpp"
#include <QUrl>

AccountSuspendedDialog::AccountSuspendedDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("Account Suspended");
	setFixedSize(500, 380);
	setStyleSheet(R"(
		QDialog { background-color: #0F172A; border: 2px solid #EF4444; border-radius: 12px; }
		QLabel { color: #F8FAFC; font-size: 14px; }
		QLabel#Title { color: #EF4444; font-size: 24px; font-weight: bold; }
		QLabel#WarningIcon { color: #EF4444; font-size: 32px; font-weight: bold; }
		QPushButton { background-color: #334155; color: white; border: none; padding: 10px 20px; border-radius: 6px; font-weight: bold; }
		QPushButton:hover { background-color: #475569; }
		QPushButton#Primary { background-color: #EF4444; }
		QPushButton#Primary:hover { background-color: #DC2626; }
		QPushButton#CloseBtn { background-color: transparent; color: #94A3B8; font-size: 18px; border: none; padding: 0px; font-weight: normal; }
		QPushButton#CloseBtn:hover { background-color: #334155; color: white; }
	)");
	setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(30, 30, 30, 30);
	layout->setSpacing(15);

	QHBoxLayout *header = new QHBoxLayout();
	QLabel *icon = new QLabel("⚠️");
	icon->setObjectName("WarningIcon");
	QLabel *title = new QLabel("Account Suspended");
	title->setObjectName("Title");
	header->addWidget(icon);
	header->addWidget(title);
	header->addStretch();
	
	QPushButton *closeBtn = new QPushButton("X");
	closeBtn->setObjectName("CloseBtn");
	closeBtn->setFixedSize(30, 30);
	header->addWidget(closeBtn);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

	layout->addLayout(header);

	QLabel *msg1 = new QLabel("Your TechRise account has been suspended.");
	layout->addWidget(msg1);

	QFrame *line = new QFrame();
	line->setFrameShape(QFrame::HLine);
	line->setStyleSheet("background-color: #334155; max-height: 1px; margin: 10px 0;");
	layout->addWidget(line);
	
	const char* userName = config_get_string(App()->GetAppConfig(), "General", "UserName");
	const char* userEmail = config_get_string(App()->GetAppConfig(), "General", "UserEmail");

	QLabel *nameLbl = new QLabel(QString("Name:  %1").arg(userName ? userName : "Unknown"));
	QLabel *emailLbl = new QLabel(QString("Email: %1").arg(userEmail ? userEmail : "Unknown"));
	QLabel *reasonLbl = new QLabel("Reason: Violation of platform policy");
	layout->addWidget(nameLbl);
	layout->addWidget(emailLbl);
	layout->addSpacing(10);
	layout->addWidget(reasonLbl);
	layout->addSpacing(10);

	QLabel *msg2 = new QLabel("If you believe this is a mistake, contact\nsupport or your administrator.");
	layout->addWidget(msg2);
	layout->addStretch();

	QHBoxLayout *btns = new QHBoxLayout();
	btns->addStretch();
	QPushButton *logoutBtn = new QPushButton("Log Out");
	QPushButton *supportBtn = new QPushButton("Contact Support");
	supportBtn->setObjectName("Primary");
	btns->addWidget(logoutBtn);
	btns->addWidget(supportBtn);
	layout->addLayout(btns);

	QObject::connect(logoutBtn, &QPushButton::clicked, this, [this]() {
		config_set_string(App()->GetAppConfig(), "General", "UserId", "");
		config_set_string(App()->GetAppConfig(), "General", "UserName", "");
		config_set_string(App()->GetAppConfig(), "General", "UserEmail", "");
		config_save_safe(App()->GetAppConfig(), "tmp", nullptr);
		this->reject();
	});

	// Poll the API every 5 seconds while this dialog is open so it closes quickly if the admin unblocks them
	QTimer *pollTimer = new QTimer(this);
	QObject::connect(pollTimer, &QTimer::timeout, LicenseManager::instance(), &LicenseManager::performValidation);
	pollTimer->start(5000);

	connect(supportBtn, &QPushButton::clicked, this, [this]() {
		QDesktopServices::openUrl(QUrl("mailto:support@techrise.com"));
		reject();
	});
}
