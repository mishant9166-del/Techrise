#include "SubscriptionRequiredDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QDesktopServices>
#include "LicenseManager.hpp"
#include <QUrl>
#include <obs.h>
#include "OBSApp.hpp"

SubscriptionRequiredDialog::SubscriptionRequiredDialog(const QString &reason, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("Subscription Required");
	setFixedSize(500, 380);
	setStyleSheet(R"(
		QDialog { background-color: #0F172A; border: 2px solid #7C3AED; border-radius: 12px; }
		QLabel { color: #F8FAFC; font-size: 14px; }
		QLabel#Title { color: #7C3AED; font-size: 24px; font-weight: bold; }
		QLabel#Icon { color: #7C3AED; font-size: 32px; font-weight: bold; }
		QPushButton { background-color: #334155; color: white; border: none; padding: 10px 20px; border-radius: 6px; font-weight: bold; }
		QPushButton:hover { background-color: #475569; }
		QPushButton#Primary { background-color: #7C3AED; }
		QPushButton#Primary:hover { background-color: #6D28D9; }
	)");
	setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(30, 30, 30, 30);
	layout->setSpacing(15);

	QHBoxLayout *header = new QHBoxLayout();
	QLabel *icon = new QLabel("⭐");
	icon->setObjectName("Icon");
	QLabel *title = new QLabel("Subscription Required");
	title->setObjectName("Title");
	
	QPushButton *closeBtn = new QPushButton("X");
	closeBtn->setFixedSize(32, 32);
	closeBtn->setStyleSheet("QPushButton { background: transparent; color: #cbd5e1; font-size: 16px; font-weight: bold; border: none; padding: 0px; } "
							"QPushButton:hover { color: white; background: #ef4444; border-radius: 4px; }");
	
	header->addWidget(icon);
	header->addWidget(title);
	header->addStretch();
	header->addWidget(closeBtn);
	layout->addLayout(header);
	
	QObject::connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

	QLabel *msg1 = new QLabel(reason);
	layout->addWidget(msg1);

	QFrame *line = new QFrame();
	line->setFrameShape(QFrame::HLine);
	line->setStyleSheet("background-color: #334155; max-height: 1px; margin: 10px 0;");
	layout->addWidget(line);
	
	config_t *appConfig = App()->GetAppConfig();
	const char* userName = config_get_string(appConfig, "General", "UserName");
	const char* userEmail = config_get_string(appConfig, "General", "UserEmail");

	QLabel *nameLbl = new QLabel(QString("Name:  %1").arg(userName ? userName : "Unknown"));
	QLabel *emailLbl = new QLabel(QString("Email: %1").arg(userEmail ? userEmail : "Unknown"));
	layout->addWidget(nameLbl);
	layout->addWidget(emailLbl);
	layout->addSpacing(10);

	QString secondMsg = "To continue using the TechRise software, please purchase\na Pro subscription from your admin dashboard.";
	if (reason == "Your Techrise subscription has expired. Please renew your license.") {
		secondMsg = "Your subscription has expired. Please renew your\nPro subscription to continue using the software.";
	}
	QLabel *msg2 = new QLabel(secondMsg);
	layout->addWidget(msg2);
	layout->addStretch();

	QHBoxLayout *btns = new QHBoxLayout();
	btns->addStretch();
	QPushButton *logoutBtn = new QPushButton("Log Out");
	QPushButton *buyBtn = new QPushButton("Contact Us");
	buyBtn->setObjectName("Primary");
	btns->addWidget(logoutBtn);
	btns->addWidget(buyBtn);
	layout->addLayout(btns);

	QObject::connect(logoutBtn, &QPushButton::clicked, this, [this, appConfig]() {
		config_set_string(appConfig, "General", "UserId", "");
		config_set_string(appConfig, "General", "UserName", "");
		config_set_string(appConfig, "General", "UserEmail", "");
		config_save_safe(appConfig, "tmp", nullptr);
		this->reject();
	});
	QObject::connect(buyBtn, &QPushButton::clicked, this, []() {
		QDesktopServices::openUrl(QUrl("https://techrise.com/contact"));
	});

	// Poll the API every 5 seconds while this dialog is open so it closes quickly after purchase
	QTimer *pollTimer = new QTimer(this);
	QObject::connect(pollTimer, &QTimer::timeout, LicenseManager::instance(), &LicenseManager::performValidation);
	pollTimer->start(5000);
}

SubscriptionRequiredDialog::~SubscriptionRequiredDialog()
{
}

