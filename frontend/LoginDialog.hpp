#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QStackedWidget>

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

    QString getUserId() const { return userId; }
    QString getUserName() const { return userName; }
    QString getUserEmail() const { return userEmail; }
    bool isAuthenticated() const { return authenticated; }

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onBuyLicenseClicked();
    void showLoginTab();
    void showRegisterTab();
    void onCloseClicked();
    void onMinimizeClicked();

private:
    void setupUi();
    void showBuyTab();
    QWidget* createLeftPanel();
    QWidget* createRightPanel();
    QWidget* createBuyPanel();
    void updateTabStyles();

    QNetworkAccessManager *networkManager;
    
    // Main Container
    QStackedWidget *stackedForms;
    QWidget *buyContainer;

    // Tabs
    QPushButton *loginTabBtn;
    QPushButton *registerTabBtn;

    // Login Form
    QLineEdit *loginEmail;
    QLineEdit *loginPhone;
    QLineEdit *loginPassword;
    QCheckBox *loginRememberMe;
    QPushButton *loginBtn;
    QLabel *loginError;

    // Register Form
    QLineEdit *regName;
    QLineEdit *regEmail;
    QLineEdit *regPhone;
    QLineEdit *regPassword;
    QLineEdit *regConfirmPassword;
    QCheckBox *regAgreeTerms;
    QPushButton *regBtn;
    QLabel *regError;

    // Buy License
    QLabel *buyStatus;
    QPushButton *buyBtn;
    QPushButton *continueBtn;

    QString userId;
    QString userName;
    QString userEmail;
    bool authenticated = false;
};
