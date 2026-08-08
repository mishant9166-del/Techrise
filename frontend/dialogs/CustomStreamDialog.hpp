#pragma once

#include <QDialog>
#include <QPointer>
#include <QList>
#include <QMap>
#include <QTimer>
#include <memory>
#include "oauth/OAuth.hpp"
#include <obs.hpp>
#include "MultiStreamAuth.hpp"

namespace Ui {
class CustomStreamDialog;
}

class CustomStreamDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomStreamDialog(QWidget *parent = nullptr);
    ~CustomStreamDialog();

private slots:
    void on_navStreamBtn_clicked();
    void on_navSettingsBtn_clicked();
    void on_subtitleSettingsBtn_clicked();
    void on_addCustomServerBtn_clicked();
    
    void on_saveSettingsBtn_clicked();
    void on_cancelSettingsBtn_clicked();
    
    void on_createServerBtn_clicked();
    void on_cancelServerBtn_clicked();
    
    void on_backServerBtn_clicked();

public:
    std::shared_ptr<Auth> GetAuth() const { return auth; }
    obs_service_t *GetService() const { return newService; }

private:
    void SetupChannels();
    void AddChannelCard(const QString &name, OAuthClient* client);

    void updateUIStatus();

    QTimer* updateTimer;

    std::shared_ptr<Auth> auth;
    OBSServiceAutoRelease newService;
    QString currentService;
    
    QScopedPointer<Ui::CustomStreamDialog> ui;
};
