#include "ActivationDialog.hpp"
#include "LicenseManager.hpp"
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QIcon>
#include <QStyle>

ActivationDialog::ActivationDialog(const QString &reason, QWidget *parent)
    : QDialog(parent)
{
    setFixedSize(600, 600);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    
    // Give it a sleek dark theme look that fits the software
    setStyleSheet(R"(
        QDialog {
            background-color: #0F172A;
            border: 2px solid #3B82F6;
            border-radius: 12px;
        }
        QLabel#Title {
            color: #F8FAFC;
            font-size: 28px;
            font-weight: bold;
        }
        QLabel#Message {
            color: #94A3B8;
            font-size: 16px;
        }
        QLabel#Icon {
            color: #3B82F6;
            font-size: 64px;
        }
        QPushButton {
            background-color: #3B82F6;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 6px;
            font-size: 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #2563EB;
        }
        QPushButton#ExitBtn {
            background-color: transparent;
            color: #64748B;
            border: 1px solid #334155;
        }
        QPushButton#ExitBtn:hover {
            background-color: #1E293B;
            color: #F8FAFC;
        }
    )");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(20);

    iconLabel = new QLabel("🔒", this);
    iconLabel->setObjectName("Icon");
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    QLabel *titleLabel = new QLabel("Activation Required", this);
    titleLabel->setObjectName("Title");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    messageLabel = new QLabel(reason, this);
    messageLabel->setObjectName("Message");
    messageLabel->setAlignment(Qt::AlignCenter);
    messageLabel->setWordWrap(true);
    layout->addWidget(messageLabel);

    layout->addSpacing(30);

    refreshButton = new QPushButton("Check Activation Status", this);
    refreshButton->setCursor(Qt::PointingHandCursor);
    connect(refreshButton, &QPushButton::clicked, this, &ActivationDialog::onRefreshClicked);
    layout->addWidget(refreshButton);

    exitButton = new QPushButton("Exit Software", this);
    exitButton->setObjectName("ExitBtn");
    exitButton->setCursor(Qt::PointingHandCursor);
    connect(exitButton, &QPushButton::clicked, this, &ActivationDialog::reject);
    layout->addWidget(exitButton);

    // Center on screen
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->geometry();
        move(screenGeometry.center() - rect().center());
    }
}

ActivationDialog::~ActivationDialog()
{
}

void ActivationDialog::onRefreshClicked()
{
    refreshButton->setEnabled(false);
    refreshButton->setText("Verifying...");

    QString errorReason;
    if (LicenseManager::instance()->ValidateOnStartup(errorReason)) {
        // Success!
        accept();
    } else {
        // Still blocked/expired
        messageLabel->setText(errorReason);
        refreshButton->setEnabled(true);
        refreshButton->setText("Check Activation Status");
        
        // Visual shake or red text indication could go here
        messageLabel->setStyleSheet("color: #EF4444; font-size: 16px; font-weight: bold;");
    }
}
