#include "LoginDialog.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QStyleOption>
#include <QDesktopServices>
#include <QUrl>

#define API_URL "https://techrise-api.onrender.com/v1"

// Custom widget for styling the main bordered container
class BorderContainer : public QWidget {
public:
    BorderContainer(QWidget* parent = nullptr) : QWidget(parent) {}
protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOption opt;
        opt.initFrom(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    }
};

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    setFixedSize(480, 520);
    setWindowTitle("TechRise Authentication");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    setStyleSheet(R"(
        #bgFrame {
            background-color: #FFFFFF;
            border: 1px solid #E5E7EB;
            border-radius: 12px;
        }
        QLabel { color: #111827; font-family: 'Segoe UI', -apple-system, sans-serif; }
        QLabel#subtitle { color: #6B7280; font-size: 12px; font-weight: normal; }
        QLabel#inputLabel { color: #111827; font-size: 11px; font-weight: 500; margin-bottom: 2px; }
        
        QLineEdit {
            background-color: #FFFFFF;
            border: 1px solid #D1D5DB;
            padding: 0px 12px;
            min-height: 34px;
            max-height: 34px;
            color: #111827;
            border-radius: 6px;
            font-size: 12px;
        }
        QLineEdit:focus {
            border: 1px solid #FBBF24;
            background-color: #FFFFFF;
        }
        
        QPushButton {
            font-family: 'Segoe UI', -apple-system, sans-serif;
            font-size: 13px;
            font-weight: 600;
            border-radius: 6px;
            border: 1px solid #D1D5DB;
            background-color: #FFFFFF;
            color: #111827;
        }
        QPushButton#primaryBtn {
            background-color: #FBBF24;
            border: none;
            color: #111827;
        }
        QPushButton#primaryBtn:hover { background-color: #F59E0B; }
        QPushButton#primaryBtn:pressed { background-color: #D97706; }
        
        QPushButton#loginTabBtn, QPushButton#registerTabBtn {
            min-height: 34px;
            max-height: 34px;
            font-size: 12px;
        }
        
        QCheckBox { color: #111827; font-size: 11px; spacing: 6px; }
        QCheckBox::indicator {
            width: 14px; height: 14px;
            border-radius: 4px;
            border: 1px solid #D1D5DB;
            background: #FFFFFF;
        }
        QCheckBox::indicator:checked {
            background: #FBBF24;
            border: 1px solid #FBBF24;
            image: url(data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>);
        }
        
        QPushButton#linkBtn {
            background-color: transparent !important;
            border: none !important;
            min-height: 20px;
            max-height: 20px;
            padding: 0px;
            margin: 0px;
            color: #FBBF24;
            font-size: 11px;
            font-weight: normal;
        }
        QPushButton#linkBtn:hover { text-decoration: underline; color: #F59E0B; }
        
        QPushButton#windowCtrlBtn {
            background-color: transparent;
            border: none;
            color: #9CA3AF;
            font-size: 12px;
            border-radius: 4px;
            min-height: 20px;
            max-height: 20px;
            min-width: 20px;
            max-width: 20px;
            padding: 0;
        }
        QPushButton#windowCtrlBtn:hover { background-color: #F3F4F6; color: #111827; }
        QPushButton#windowCtrlBtn#closeBtn:hover { background-color: #EF4444; color: white; }
        
        #MainFormsBox {
            background-color: transparent;
            border: none;
        }
    )");

    networkManager = new QNetworkAccessManager(this);
    setupUi();

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->geometry();
        move(screenGeometry.center() - rect().center());
    }
}

LoginDialog::~LoginDialog() {}

void LoginDialog::setupUi()
{
    // We must put everything in a BorderContainer to properly render a bordered background on a translucent window
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    
    BorderContainer *bgFrame = new BorderContainer(this);
    bgFrame->setObjectName("bgFrame");
    rootLayout->addWidget(bgFrame);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(bgFrame);
    mainLayout->setContentsMargins(12, 10, 12, 12);
    mainLayout->setSpacing(0);
    
    // WINDOW CONTROLS
    QHBoxLayout *ctrlLayout = new QHBoxLayout();
    ctrlLayout->setContentsMargins(0,0,0,0);
    ctrlLayout->setSpacing(4);
    ctrlLayout->addStretch();
    QPushButton *minBtn = new QPushButton("—");
    minBtn->setObjectName("windowCtrlBtn");
    minBtn->setFlat(true);
    minBtn->setCursor(Qt::PointingHandCursor);
    QPushButton *closeBtn = new QPushButton("✕");
    closeBtn->setObjectName("windowCtrlBtn");
    closeBtn->setFlat(true);
    closeBtn->setCursor(Qt::PointingHandCursor);
    ctrlLayout->addWidget(minBtn);
    ctrlLayout->addWidget(closeBtn);
    mainLayout->addLayout(ctrlLayout);
    
    connect(minBtn, &QPushButton::clicked, this, &LoginDialog::onMinimizeClicked);
    connect(closeBtn, &QPushButton::clicked, this, &LoginDialog::onCloseClicked);
    
    // TOP LOGO
    QLabel *logoLabel = new QLabel(this);
    QPixmap logoPix("F:/Techrise/frontend/forms/images/logo new site.png");
    logoLabel->setPixmap(logoPix.scaledToHeight(60, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(logoLabel);
    
    mainLayout->addSpacing(16);
    
    // TABS (Functioning Buttons)
    QHBoxLayout *tabsLayout = new QHBoxLayout();
    tabsLayout->setAlignment(Qt::AlignCenter);
    tabsLayout->setSpacing(0);
    tabsLayout->addStretch();
    
    loginTabBtn = new QPushButton("Log In");
    loginTabBtn->setObjectName("loginTabBtn");
    loginTabBtn->setFixedSize(130, 34);
    loginTabBtn->setCursor(Qt::PointingHandCursor);
    
    registerTabBtn = new QPushButton("Register");
    registerTabBtn->setObjectName("registerTabBtn");
    registerTabBtn->setFixedSize(130, 34);
    registerTabBtn->setCursor(Qt::PointingHandCursor);
    
    tabsLayout->addWidget(loginTabBtn);
    tabsLayout->addWidget(registerTabBtn);
    tabsLayout->addStretch();
    mainLayout->addLayout(tabsLayout);
    
    mainLayout->addSpacing(16);
    
    // MAIN CONTENT BOX
    BorderContainer *formsContainer = new BorderContainer(this);
    formsContainer->setObjectName("MainFormsBox");
    QVBoxLayout *formsLayout = new QVBoxLayout(formsContainer);
    formsLayout->setContentsMargins(30, 0, 30, 0); // Side padding for forms
    formsLayout->setSpacing(0);
    
    stackedForms = new QStackedWidget();
    stackedForms->addWidget(createLeftPanel());   // Index 0: Login
    stackedForms->addWidget(createRightPanel());  // Index 1: Register
    formsLayout->addWidget(stackedForms);
    
    mainLayout->addWidget(formsContainer);
    
    // BUY CONTAINER (Hidden by default)
    buyContainer = createBuyPanel();
    buyContainer->hide();
    mainLayout->addWidget(buyContainer);

    // Connections
    connect(loginTabBtn, &QPushButton::clicked, this, &LoginDialog::showLoginTab);
    connect(registerTabBtn, &QPushButton::clicked, this, &LoginDialog::showRegisterTab);

    // Initial state
    showLoginTab();
}

void LoginDialog::updateTabStyles() {
    QString activeStyle = "color: #111827; font-weight: 600; font-size: 13px; background-color: #FBBF24; border: 1px solid #FBBF24; border-radius: 0px;";
    QString inactiveStyle = "color: #111827; font-weight: 500; font-size: 13px; background-color: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 0px;";
    
    if (stackedForms->currentIndex() == 0) {
        loginTabBtn->setStyleSheet(activeStyle + "border-top-left-radius: 6px;");
        registerTabBtn->setStyleSheet(inactiveStyle + "border-top-right-radius: 6px; border-left: none;");
    } else {
        loginTabBtn->setStyleSheet(inactiveStyle + "border-top-left-radius: 6px; border-right: none;");
        registerTabBtn->setStyleSheet(activeStyle + "border-top-right-radius: 6px;");
    }
}

void LoginDialog::showLoginTab() {
    stackedForms->setCurrentIndex(0);
    updateTabStyles();
}

void LoginDialog::showRegisterTab() {
    stackedForms->setCurrentIndex(1);
    updateTabStyles();
}

QWidget* LoginDialog::createLeftPanel() {
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    QLabel *title = new QLabel("Welcome Back!");
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: #111827;");
    title->setAlignment(Qt::AlignCenter);
    QLabel *subTitle = new QLabel("Log in to your TechRise account");
    subTitle->setObjectName("subtitle");
    subTitle->setAlignment(Qt::AlignCenter);
    
    layout->addWidget(title);
    layout->addWidget(subTitle);
    layout->addSpacing(20);
    
    QLabel *lblEmail = new QLabel("Email or phone number");
    lblEmail->setObjectName("inputLabel");
    loginEmail = new QLineEdit();
    loginEmail->setPlaceholderText("Email or phone number");
    
    layout->addWidget(lblEmail);
    layout->addWidget(loginEmail);
    layout->addSpacing(10);
    
    QLabel *lblPass = new QLabel("Password");
    lblPass->setObjectName("inputLabel");
    loginPassword = new QLineEdit();
    loginPassword->setPlaceholderText("Enter your password");
    loginPassword->setEchoMode(QLineEdit::Password);
    
    layout->addWidget(lblPass);
    layout->addWidget(loginPassword);
    layout->addSpacing(6);
    
    // Remember Me + Forgot Password
    QHBoxLayout *optionsLayout = new QHBoxLayout();
    optionsLayout->setContentsMargins(0,0,0,0);
    loginRememberMe = new QCheckBox("Remember me");
    loginRememberMe->setChecked(true);
    QPushButton *forgotBtn = new QPushButton("Forgot Password?");
    forgotBtn->setObjectName("linkBtn");
    forgotBtn->setStyleSheet("background-color: transparent; border: none; color: #60A5FA; text-align: right;");
    forgotBtn->setFlat(true);
    forgotBtn->setCursor(Qt::PointingHandCursor);
    optionsLayout->addWidget(loginRememberMe);
    optionsLayout->addStretch();
    optionsLayout->addWidget(forgotBtn);
    layout->addLayout(optionsLayout);
    
    connect(forgotBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://app.techrise.me/forgot-password"));
    });
    
    layout->addSpacing(12);
    
    loginError = new QLabel("");
    loginError->setStyleSheet("color: #EF4444; font-size: 11px;");
    layout->addWidget(loginError);
    
    loginBtn = new QPushButton("Log In");
    loginBtn->setObjectName("primaryBtn");
    loginBtn->setCursor(Qt::PointingHandCursor);
    connect(loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    layout->addWidget(loginBtn);
    

    
    layout->addStretch();
    return panel;
}

QWidget* LoginDialog::createRightPanel() {
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    QLabel *title = new QLabel("Create Account");
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: #111827;");
    title->setAlignment(Qt::AlignCenter);
    QLabel *subTitle = new QLabel("Join TechRise and get started");
    subTitle->setObjectName("subtitle");
    subTitle->setAlignment(Qt::AlignCenter);
    
    layout->addWidget(title);
    layout->addWidget(subTitle);
    layout->addSpacing(16);
    
    QLabel *lblName = new QLabel("Full Name");
    lblName->setObjectName("inputLabel");
    regName = new QLineEdit();
    regName->setPlaceholderText("Enter your full name");
    
    layout->addWidget(lblName);
    layout->addWidget(regName);
    layout->addSpacing(8);
    
    QHBoxLayout *emailPhoneLayout = new QHBoxLayout();
    emailPhoneLayout->setContentsMargins(0,0,0,0);
    emailPhoneLayout->setSpacing(10);
    
    QVBoxLayout *emailCol = new QVBoxLayout();
    emailCol->setContentsMargins(0,0,0,0);
    emailCol->setSpacing(0);
    QLabel *lblEmail = new QLabel("Email Address");
    lblEmail->setObjectName("inputLabel");
    regEmail = new QLineEdit();
    regEmail->setPlaceholderText("Enter your email");
    emailCol->addWidget(lblEmail);
    emailCol->addWidget(regEmail);
    
    QVBoxLayout *phoneCol = new QVBoxLayout();
    phoneCol->setContentsMargins(0,0,0,0);
    phoneCol->setSpacing(0);
    QLabel *lblPhone = new QLabel("Phone Number");
    lblPhone->setObjectName("inputLabel");
    regPhone = new QLineEdit();
    regPhone->setPlaceholderText("Phone (optional)");
    phoneCol->addWidget(lblPhone);
    phoneCol->addWidget(regPhone);
    
    emailPhoneLayout->addLayout(emailCol);
    emailPhoneLayout->addLayout(phoneCol);
    
    layout->addLayout(emailPhoneLayout);
    layout->addSpacing(8);
    
    QHBoxLayout *passLayout = new QHBoxLayout();
    passLayout->setContentsMargins(0,0,0,0);
    passLayout->setSpacing(10);
    
    QVBoxLayout *passCol = new QVBoxLayout();
    passCol->setContentsMargins(0,0,0,0);
    passCol->setSpacing(0);
    QLabel *lblPass = new QLabel("Password");
    lblPass->setObjectName("inputLabel");
    regPassword = new QLineEdit();
    regPassword->setPlaceholderText("Create password");
    regPassword->setEchoMode(QLineEdit::Password);
    passCol->addWidget(lblPass);
    passCol->addWidget(regPassword);
    
    QVBoxLayout *confCol = new QVBoxLayout();
    confCol->setContentsMargins(0,0,0,0);
    confCol->setSpacing(0);
    QLabel *lblConf = new QLabel("Confirm Password");
    lblConf->setObjectName("inputLabel");
    regConfirmPassword = new QLineEdit();
    regConfirmPassword->setPlaceholderText("Confirm password");
    regConfirmPassword->setEchoMode(QLineEdit::Password);
    confCol->addWidget(lblConf);
    confCol->addWidget(regConfirmPassword);
    
    passLayout->addLayout(passCol);
    passLayout->addLayout(confCol);
    
    layout->addLayout(passLayout);
    layout->addSpacing(8);
    
    // Terms check removed as requested
    
    layout->addSpacing(6);
    
    regError = new QLabel("");
    regError->setStyleSheet("color: #EF4444; font-size: 11px;");
    layout->addWidget(regError);
    
    regBtn = new QPushButton("Create Account");
    regBtn->setObjectName("primaryBtn");
    regBtn->setCursor(Qt::PointingHandCursor);
    connect(regBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    layout->addWidget(regBtn);
    
    layout->addStretch();
    return panel;
}

QWidget* LoginDialog::createBuyPanel() {
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setAlignment(Qt::AlignCenter);
    
    buyStatus = new QLabel("Account linked! Please purchase a license to continue.");
    buyStatus->setStyleSheet("font-size: 16px; color: #10B981;");
    buyStatus->setAlignment(Qt::AlignCenter);
    buyStatus->setWordWrap(true);
    
    buyBtn = new QPushButton("Buy License Now");
    buyBtn->setObjectName("primaryBtn");
    buyBtn->setFixedSize(220, 40);
    buyBtn->setCursor(Qt::PointingHandCursor);
    
    continueBtn = new QPushButton("Continue to OBS");
    continueBtn->setObjectName("googleBtn");
    continueBtn->setFixedSize(220, 40);
    continueBtn->setEnabled(false);
    
    layout->addStretch();
    layout->addWidget(buyStatus, 0, Qt::AlignCenter);
    layout->addSpacing(15);
    layout->addWidget(buyBtn, 0, Qt::AlignCenter);
    layout->addSpacing(10);
    layout->addWidget(continueBtn, 0, Qt::AlignCenter);
    layout->addStretch();
    
    connect(buyBtn, &QPushButton::clicked, this, &LoginDialog::onBuyLicenseClicked);
    connect(continueBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    return panel;
}

void LoginDialog::showBuyTab() {
    stackedForms->hide();
    loginTabBtn->hide();
    registerTabBtn->hide();
    buyContainer->show();
}

void LoginDialog::onLoginClicked()
{
    QString identifier = loginEmail->text().trimmed();
    if (identifier.isEmpty()) {
        loginError->setText("Please enter email or phone.");
        return;
    }
    
    bool isOnlyDigits = true;
    for (QChar c : identifier) {
        if (!c.isDigit()) { isOnlyDigits = false; break; }
    }
    if (isOnlyDigits && identifier.length() != 10) {
        loginError->setText("Phone number must be exactly 10 digits.");
        return;
    }

    loginError->setText("Logging in...");
    loginBtn->setEnabled(false);

    QNetworkRequest request(QUrl(QString(API_URL) + "/auth/login"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    payload["email_or_phone"] = identifier;
    payload["password"] = loginPassword->text();
    QByteArray postData = QJsonDocument(payload).toJson();

    QNetworkReply *reply = networkManager->post(request, postData);
    
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    loginBtn->setEnabled(true);

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument res = QJsonDocument::fromJson(reply->readAll());
        if (res.object()["success"].toBool()) {
            userId = res.object()["user_id"].toString();
            userName = res.object()["user_name"].toString();
            if (userName.isEmpty()) userName = res.object()["name"].toString();
            userEmail = res.object()["email"].toString();
            if (userEmail.isEmpty()) userEmail = loginEmail->text();
            authenticated = true;
            accept();
        } else {
            if (res.isObject() && res.object().contains("error")) {
                loginError->setText(res.object()["error"].toString());
            } else {
                loginError->setText("Invalid credentials.");
            }
        }
    } else {
        QJsonDocument res = QJsonDocument::fromJson(reply->readAll());
        if (res.isObject() && res.object().contains("error")) {
            loginError->setText(res.object()["error"].toString());
        } else {
            loginError->setText("Network error: " + reply->errorString());
        }
    }
    reply->deleteLater();
}

void LoginDialog::onRegisterClicked()
{
    QString rEmail = regEmail->text().trimmed();
    QString rPhone = regPhone->text().trimmed();

    if (rEmail.isEmpty() && rPhone.isEmpty()) {
        regError->setText("Please enter email or phone.");
        return;
    }

    if (!rPhone.isEmpty()) {
        bool isOnlyDigits = true;
        for (QChar c : rPhone) {
            if (!c.isDigit()) { isOnlyDigits = false; break; }
        }
        if (!isOnlyDigits || rPhone.length() != 10) {
            regError->setText("Phone number must be exactly 10 digits.");
            return;
        }
    }

    regError->setText("Registering...");
    regBtn->setEnabled(false);

    if (regPassword->text() != regConfirmPassword->text()) {
        regError->setText("Passwords do not match.");
        regBtn->setEnabled(true);
        return;
    }

    // Removed Terms check

    QNetworkRequest request(QUrl(QString(API_URL) + "/auth/register"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    if (!rEmail.isEmpty()) payload["email"] = rEmail;
    if (!rPhone.isEmpty()) payload["phone"] = rPhone;
    payload["password"] = regPassword->text();
    payload["name"] = regName->text();
    QByteArray postData = QJsonDocument(payload).toJson();

    QNetworkReply *reply = networkManager->post(request, postData);
    
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    regBtn->setEnabled(true);

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument res = QJsonDocument::fromJson(reply->readAll());
        if (res.object()["success"].toBool()) {
            userId = res.object()["user_id"].toString();
            userName = res.object()["user_name"].toString();
            if (userName.isEmpty()) userName = res.object()["name"].toString();
            if (userName.isEmpty()) userName = regName->text();
            userEmail = res.object()["email"].toString();
            if (userEmail.isEmpty()) userEmail = regEmail->text();
            authenticated = true;
            accept();
        } else {
            if (res.isObject() && res.object().contains("error")) {
                regError->setText(res.object()["error"].toString());
            } else {
                regError->setText("Registration failed (maybe email exists).");
            }
        }
    } else {
        QJsonDocument res = QJsonDocument::fromJson(reply->readAll());
        if (res.isObject() && res.object().contains("error")) {
            regError->setText(res.object()["error"].toString());
        } else {
            regError->setText("Network error: " + reply->errorString());
        }
    }
    reply->deleteLater();
}

void LoginDialog::onBuyLicenseClicked()
{
    buyBtn->setEnabled(false);
    buyBtn->setText("Purchasing...");

    QNetworkRequest request(QUrl(QString(API_URL) + "/licenses/buy"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    payload["user_id"] = userId;
    payload["plan"] = "Pro Monthly";
    QByteArray postData = QJsonDocument(payload).toJson();

    QNetworkReply *reply = networkManager->post(request, postData);
    
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        buyStatus->setText("License purchased successfully! Welcome to TechRise.");
        buyBtn->hide();
        continueBtn->setEnabled(true);
        continueBtn->setObjectName("primaryBtn");
        continueBtn->style()->unpolish(continueBtn);
        continueBtn->style()->polish(continueBtn);
        continueBtn->setCursor(Qt::PointingHandCursor);
    } else {
        buyStatus->setText("Purchase failed. Please try again.");
        buyStatus->setStyleSheet("color: #EF4444; font-size: 16px;");
        buyBtn->setEnabled(true);
        buyBtn->setText("Buy License Now");
    }
    reply->deleteLater();
}

void LoginDialog::onCloseClicked() {
    reject();
}

void LoginDialog::onMinimizeClicked() {
    showMinimized();
}

