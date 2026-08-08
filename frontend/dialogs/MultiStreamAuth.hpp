#pragma once

#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QUrl>
#include <obs-frontend-api.h>

struct PlatformSession {
    bool loggedIn = false;
    QString accessToken;
    QString refreshToken;
    qint64 expiryTime = 0;
    
    // User Profile
    QString userName;
    QString email;
    QString profilePicture;
    QString channelId;
    QString broadcasterId;
    QString extraData; // Followers/Subscribers/Pages
    
    // Stream Settings
    bool streamEnabled = false;
    QString streamTitle;
    QString streamDescription;
    QString streamPrivacy;
    QString streamCategory;
};

class OAuthCallbackServer : public QTcpServer {
    Q_OBJECT
public:
    OAuthCallbackServer(QObject* parent = nullptr);
    ~OAuthCallbackServer();

signals:
    void codeReceived(const QString& code, const QString& state);

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};

class OAuthClient : public QObject {
    Q_OBJECT
public:
    OAuthClient(const QString& platform, QObject* parent = nullptr);
    ~OAuthClient();

    void startLogin();
    void logout();
    bool isLoggedIn() const;
    QString getPlatform() const { return platform; }
    PlatformSession getSession() const { return session; }
    void updateSession(const PlatformSession& newSession); // For UI updates
    
    virtual void prepareStream(std::function<void(QString rtmpUrl, QString streamKey)> callback) = 0;

    void loadTokens();
    void saveTokens();
    
    // Call this on OBS start to automatically log in and fetch profile
    void restoreSession();

signals:
    void loginSuccess();
    void loginFailed(const QString& error);
    void profileFetched();
    void sessionUpdated(); // Emitted whenever UI needs to repaint

protected:
    virtual QString getAuthUrl() const = 0;
    virtual QString getTokenUrl() const = 0;
    virtual QUrlQuery getTokenRequestParams(const QString& code) const = 0;
    virtual void fetchProfile() = 0;
    
    virtual void refreshAccessToken() = 0; // Each API has slightly different refresh parameters

    void exchangeCodeForToken(const QString& code);

    QString platform;
    QString clientId;
    QString clientSecret;
    
    QNetworkAccessManager* networkManager;
    OAuthCallbackServer* callbackServer;
    
    PlatformSession session;

private slots:
    void onCodeReceived(const QString& code, const QString& state);
};

class TwitchOAuthClient : public OAuthClient {
    Q_OBJECT
public:
    TwitchOAuthClient(QObject* parent = nullptr);
    void prepareStream(std::function<void(QString, QString)> callback) override;
protected:
    QString getAuthUrl() const override;
    QString getTokenUrl() const override;
    QUrlQuery getTokenRequestParams(const QString& code) const override;
    void fetchProfile() override;
    void refreshAccessToken() override;
};

class YouTubeOAuthClient : public OAuthClient {
    Q_OBJECT
public:
    YouTubeOAuthClient(QObject* parent = nullptr);
    void prepareStream(std::function<void(QString, QString)> callback) override;
protected:
    QString getAuthUrl() const override;
    QString getTokenUrl() const override;
    QUrlQuery getTokenRequestParams(const QString& code) const override;
    void fetchProfile() override;
    void refreshAccessToken() override;
};

class FacebookOAuthClient : public OAuthClient {
    Q_OBJECT
public:
    FacebookOAuthClient(QObject* parent = nullptr);
    void prepareStream(std::function<void(QString, QString)> callback) override;
protected:
    QString getAuthUrl() const override;
    QString getTokenUrl() const override;
    QUrlQuery getTokenRequestParams(const QString& code) const override;
    void fetchProfile() override;
    void refreshAccessToken() override;
};

class PlatformSessionManager : public QObject {
    Q_OBJECT
public:
    static PlatformSessionManager* Get();

    TwitchOAuthClient* getTwitch() { return twitchClient; }
    YouTubeOAuthClient* getYouTube() { return youtubeClient; }
    FacebookOAuthClient* getFacebook() { return facebookClient; }

    void restoreAllSessions();
    void saveAllSessions();

    void initHooks();
    void startCustomStreams();
    void stopCustomStreams();
    void startStream(OAuthClient* client);
    void stopStream(OAuthClient* client);
    bool isPlatformActive(const QString& platformName);
    int getPlatformState(const QString& platformName);

    enum class OutputState { Stopped, Starting, Active, Reconnecting };
    QMap<QString, OutputState> platformStates;

private:
    static void OBSFrontendEvent(enum obs_frontend_event event, void *private_data);
    void startPlatformOutput(OAuthClient* client, const QString& rtmpUrl, const QString& streamKey);

    PlatformSessionManager();
    ~PlatformSessionManager();

    TwitchOAuthClient* twitchClient;
    YouTubeOAuthClient* youtubeClient;
    FacebookOAuthClient* facebookClient;
    
    QMap<QString, obs_output_t*> customOutputs;
    QString mainStreamPlatform;
};
