#include "MultiStreamAuth.hpp"
#include <QEventLoop>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>
#include <QMessageBox>
#include <util/config-file.h>
#include <QDateTime>

// OAuthCallbackServer implementation
OAuthCallbackServer::OAuthCallbackServer(QObject* parent) : QTcpServer(parent) {}
OAuthCallbackServer::~OAuthCallbackServer() {}

void OAuthCallbackServer::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket* socket = new QTcpSocket(this);
    socket->setSocketDescriptor(socketDescriptor);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QString request = QString::fromUtf8(socket->readAll());
        if (request.startsWith("GET")) {
            int firstSpace = request.indexOf(" ");
            int secondSpace = request.indexOf(" ", firstSpace + 1);
            if (firstSpace != -1 && secondSpace != -1) {
                QString urlStr = request.mid(firstSpace + 1, secondSpace - firstSpace - 1);
                QUrl url("http://localhost" + urlStr);
                QUrlQuery query(url.query());
                QString code = query.queryItemValue("code");
                QString state = query.queryItemValue("state");
                
                QString response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><h1>Authentication Successful!</h1><p>You can close this window and return to OBS.</p></body></html>";
                socket->write(response.toUtf8());
                socket->flush();
                socket->waitForBytesWritten(1000);
                
                if (!code.isEmpty()) {
                    emit codeReceived(code, state);
                }
            }
        }
        socket->disconnectFromHost();
        socket->deleteLater();
    });
}

// OAuthClient implementation
OAuthClient::OAuthClient(const QString& platform, QObject* parent) 
    : QObject(parent), platform(platform), networkManager(new QNetworkAccessManager(this)), callbackServer(nullptr) {
}

OAuthClient::~OAuthClient() {
    if (callbackServer) {
        callbackServer->close();
        callbackServer->deleteLater();
    }
}

void OAuthClient::startLogin() {
    if (!callbackServer) {
        callbackServer = new OAuthCallbackServer(this);
        connect(callbackServer, &OAuthCallbackServer::codeReceived, this, &OAuthClient::onCodeReceived);
    }
    if (!callbackServer->isListening()) {
        callbackServer->listen(QHostAddress::Any, 8080);
    }
    QDesktopServices::openUrl(QUrl(getAuthUrl()));
}

void OAuthClient::logout() {
    session = PlatformSession();
    saveTokens();
    emit sessionUpdated();
}

bool OAuthClient::isLoggedIn() const {
    return session.loggedIn;
}

void OAuthClient::updateSession(const PlatformSession& newSession) {
    session = newSession;
    saveTokens();
    emit sessionUpdated();
}

void OAuthClient::onCodeReceived(const QString& code, const QString& state) {
    if (callbackServer) callbackServer->close();
    exchangeCodeForToken(code);
}

void OAuthClient::exchangeCodeForToken(const QString& code) {
    QNetworkRequest request{QUrl(getTokenUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery params = getTokenRequestParams(code);
    QNetworkReply* reply = networkManager->post(request, params.toString().toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            session.accessToken = obj["access_token"].toString();
            if (obj.contains("refresh_token")) session.refreshToken = obj["refresh_token"].toString();
            if (obj.contains("expires_in")) session.expiryTime = QDateTime::currentSecsSinceEpoch() + obj["expires_in"].toInt();
            
            session.loggedIn = true;
            saveTokens();
            emit loginSuccess();
            emit sessionUpdated();
            fetchProfile();
        } else {
            QString errBody = reply->readAll();
            QMessageBox::critical(nullptr, "Token Exchange Error", QString("Error: %1\nBody: %2").arg(reply->errorString(), errBody));
            emit loginFailed(reply->errorString());
            emit sessionUpdated(); // Update UI even on failure
        }
        reply->deleteLater();
    });
}

#include <QSettings>

void OAuthClient::saveTokens() {
    QSettings settings("Techrise", "MultiStreamAuth");
    QString section = "MultiStream_" + platform;
    settings.beginGroup(section);
    settings.setValue("LoggedIn", session.loggedIn);
    settings.setValue("AccessToken", session.accessToken);
    settings.setValue("RefreshToken", session.refreshToken);
    settings.setValue("ExpiryTime", session.expiryTime);
    settings.setValue("UserName", session.userName);
    settings.setValue("Email", session.email);
    settings.setValue("ProfilePicture", session.profilePicture);
    settings.setValue("ExtraData", session.extraData);
    settings.setValue("StreamEnabled", session.streamEnabled);
    settings.setValue("StreamTitle", session.streamTitle);
    settings.setValue("StreamDescription", session.streamDescription);
    settings.setValue("StreamPrivacy", session.streamPrivacy);
    settings.setValue("StreamCategory", session.streamCategory);
    settings.endGroup();
}

void OAuthClient::loadTokens() {
    QSettings settings("Techrise", "MultiStreamAuth");
    QString section = "MultiStream_" + platform;
    settings.beginGroup(section);
    session.loggedIn = settings.value("LoggedIn", false).toBool();
    session.accessToken = settings.value("AccessToken", "").toString();
    session.refreshToken = settings.value("RefreshToken", "").toString();
    session.expiryTime = settings.value("ExpiryTime", 0).toLongLong();
    session.userName = settings.value("UserName", "").toString();
    session.email = settings.value("Email", "").toString();
    session.profilePicture = settings.value("ProfilePicture", "").toString();
    session.extraData = settings.value("ExtraData", "").toString();
    session.streamEnabled = settings.value("StreamEnabled", false).toBool();
    session.streamTitle = settings.value("StreamTitle", "").toString();
    session.streamDescription = settings.value("StreamDescription", "").toString();
    session.streamPrivacy = settings.value("StreamPrivacy", "").toString();
    session.streamCategory = settings.value("StreamCategory", "").toString();
    settings.endGroup();
}

void OAuthClient::restoreSession() {
    loadTokens();
    if (session.loggedIn && !session.accessToken.isEmpty()) {
        if (session.expiryTime > 0 && QDateTime::currentSecsSinceEpoch() >= session.expiryTime) {
            refreshAccessToken();
        } else {
            fetchProfile();
        }
    }
}

// PlatformSessionManager
PlatformSessionManager* PlatformSessionManager::Get() {
    static PlatformSessionManager instance;
    return &instance;
}
PlatformSessionManager::PlatformSessionManager() {
    twitchClient = new TwitchOAuthClient(this);
    youtubeClient = new YouTubeOAuthClient(this);
    facebookClient = new FacebookOAuthClient(this);
    initHooks();
}

void PlatformSessionManager::initHooks() {
    obs_frontend_add_event_callback(OBSFrontendEvent, this);
}

static void on_custom_output_starting(void *data, calldata_t *cd) {
    (void)cd;
    OAuthClient* client = static_cast<OAuthClient*>(data);
    PlatformSessionManager::Get()->platformStates[client->getPlatform()] = PlatformSessionManager::OutputState::Starting;
}

static void on_custom_output_start(void *data, calldata_t *cd) {
    (void)cd;
    OAuthClient* client = static_cast<OAuthClient*>(data);
    PlatformSessionManager::Get()->platformStates[client->getPlatform()] = PlatformSessionManager::OutputState::Active;
}

static void on_custom_output_stop(void *data, calldata_t *cd) {
    (void)cd;
    OAuthClient* client = static_cast<OAuthClient*>(data);
    PlatformSessionManager::Get()->platformStates[client->getPlatform()] = PlatformSessionManager::OutputState::Stopped;
}

static void on_custom_output_reconnect(void *data, calldata_t *cd) {
    (void)cd;
    OAuthClient* client = static_cast<OAuthClient*>(data);
    PlatformSessionManager::Get()->platformStates[client->getPlatform()] = PlatformSessionManager::OutputState::Reconnecting;
}

static void on_custom_output_reconnect_success(void *data, calldata_t *cd) {
    (void)cd;
    OAuthClient* client = static_cast<OAuthClient*>(data);
    PlatformSessionManager::Get()->platformStates[client->getPlatform()] = PlatformSessionManager::OutputState::Active;
}

void PlatformSessionManager::OBSFrontendEvent(enum obs_frontend_event event, void *private_data) {
    PlatformSessionManager* manager = static_cast<PlatformSessionManager*>(private_data);
    blog(LOG_INFO, "[MultiStream] OBSFrontendEvent triggered: %d, mainStreamPlatform before: %s", (int)event, manager->mainStreamPlatform.toUtf8().constData());
    if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING) {
        if (manager->mainStreamPlatform.isEmpty()) {
            obs_service_t* service = obs_frontend_get_streaming_service();
            if (service) {
                obs_data_t* settings = obs_service_get_settings(service);
                const char* server = obs_data_get_string(settings, "server");
                const char* svc = obs_data_get_string(settings, "service");
                QString url = (server ? QString(server) : "") + " " + (svc ? QString(svc) : "");
                if (url.contains("twitch", Qt::CaseInsensitive)) manager->mainStreamPlatform = "Twitch";
                else if (url.contains("youtube", Qt::CaseInsensitive)) manager->mainStreamPlatform = "YouTube";
                else if (url.contains("facebook", Qt::CaseInsensitive)) manager->mainStreamPlatform = "Facebook";
                obs_data_release(settings);
                obs_service_release(service);
            }
        }
        blog(LOG_INFO, "[MultiStream] STREAMING_STARTING, mainStreamPlatform after deduction: %s", manager->mainStreamPlatform.toUtf8().constData());
        if (!manager->mainStreamPlatform.isEmpty()) {
            manager->platformStates[manager->mainStreamPlatform] = OutputState::Starting;
            OAuthClient* mainClient = nullptr;
            if (manager->mainStreamPlatform == "Twitch") mainClient = manager->twitchClient;
            else if (manager->mainStreamPlatform == "YouTube") mainClient = manager->youtubeClient;
            else if (manager->mainStreamPlatform == "Facebook") mainClient = manager->facebookClient;
            
            if (mainClient) {
                PlatformSession session = mainClient->getSession();
                if (!session.streamEnabled) {
                    session.streamEnabled = true;
                    mainClient->updateSession(session);
                    mainClient->saveTokens();
                }
            }
        }
    } else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
        blog(LOG_INFO, "[MultiStream] STREAMING_STARTED, mainStreamPlatform: %s", manager->mainStreamPlatform.toUtf8().constData());
        if (!manager->mainStreamPlatform.isEmpty()) {
            manager->platformStates[manager->mainStreamPlatform] = OutputState::Active;
        }
        obs_output_t* mainOut = obs_frontend_get_streaming_output();
        if (mainOut) {
            obs_output_release(mainOut);
        }
        manager->startCustomStreams();
    } else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING) {
        blog(LOG_INFO, "[MultiStream] STREAMING_STOPPING, mainStreamPlatform: %s", manager->mainStreamPlatform.toUtf8().constData());
        if (!manager->mainStreamPlatform.isEmpty()) {
            manager->platformStates[manager->mainStreamPlatform] = OutputState::Stopped;
        }
    } else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
        blog(LOG_INFO, "[MultiStream] STREAMING_STOPPED, mainStreamPlatform: %s", manager->mainStreamPlatform.toUtf8().constData());
        if (!manager->mainStreamPlatform.isEmpty()) {
            manager->platformStates[manager->mainStreamPlatform] = OutputState::Stopped;
        }
        obs_output_t* mainOut = obs_frontend_get_streaming_output();
        if (mainOut) {
            obs_output_release(mainOut);
        }
        manager->stopCustomStreams();
        manager->mainStreamPlatform = "";
    }
}

void PlatformSessionManager::startPlatformOutput(OAuthClient* client, const QString& rtmpUrl, const QString& streamKey) {
    if (rtmpUrl.isEmpty()) return;
    QString platName = client->getPlatform();
    
    if (platName == mainStreamPlatform) {
        blog(LOG_INFO, "[MultiStream] startPlatformOutput: Skipped %s because it is the main stream", platName.toUtf8().constData());
        return;
    }
    
    if (customOutputs.contains(platName)) {
        stopStream(client);
    }
    
    obs_output_t* out = obs_output_create("rtmp_output", (platName + "_output").toUtf8().constData(), nullptr, nullptr);
    if (!out) {
        blog(LOG_ERROR, "[MultiStream] startPlatformOutput: FAILED to create rtmp_output for %s!", platName.toUtf8().constData());
        return;
    }
    
    signal_handler_t *sh = obs_output_get_signal_handler(out);
    signal_handler_connect(sh, "starting", on_custom_output_starting, client);
    signal_handler_connect(sh, "start", on_custom_output_start, client);
    signal_handler_connect(sh, "stop", on_custom_output_stop, client);
    signal_handler_connect(sh, "reconnect", on_custom_output_reconnect, client);
    signal_handler_connect(sh, "reconnect_success", on_custom_output_reconnect_success, client);
    
    // We don't connect 'stop' to stop the main stream anymore.
    
    obs_service_t* service = obs_service_create("rtmp_custom", (platName + "_service").toUtf8().constData(), nullptr, nullptr);
    if (service) {
        blog(LOG_INFO, "[MultiStream] startPlatformOutput: successfully created rtmp_custom service for %s", platName.toUtf8().constData());
        obs_data_t* settings = obs_data_create();
        obs_data_set_string(settings, "server", rtmpUrl.toUtf8().constData());
        obs_data_set_string(settings, "key", streamKey.toUtf8().constData());
        obs_service_update(service, settings);
        obs_data_release(settings);
        
        obs_output_set_service(out, service);
        // DO NOT release service here, the output relies on this reference!
        // We will release it when the output is stopped/destroyed.
    } else {
        blog(LOG_ERROR, "[MultiStream] startPlatformOutput: FAILED to create rtmp_custom service for %s!", platName.toUtf8().constData());
    }
    
    obs_output_t* mainOut = obs_frontend_get_streaming_output();
    if (mainOut) {
        obs_encoder_t* v_enc = obs_output_get_video_encoder(mainOut);
        obs_encoder_t* a_enc = obs_output_get_audio_encoder(mainOut, 0);
        if (v_enc) obs_output_set_video_encoder(out, v_enc);
        if (a_enc) obs_output_set_audio_encoder(out, a_enc, 0);
        
        bool v_active = v_enc ? obs_encoder_active(v_enc) : false;
        bool a_active = a_enc ? obs_encoder_active(a_enc) : false;
        
        blog(LOG_INFO, "[MultiStream] startPlatformOutput: setting encoders from mainOut (v_enc: %p [active:%s], a_enc: %p [active:%s]). URL: %s", 
             v_enc, v_active ? "yes" : "no", a_enc, a_active ? "yes" : "no", rtmpUrl.toUtf8().constData());
        
        obs_output_release(mainOut);
    } else {
        blog(LOG_WARNING, "[MultiStream] startPlatformOutput: mainOut is NULL!");
    }
    
    customOutputs[platName] = out;
    
    obs_service_t* currentService = obs_output_get_service(out);
    if (currentService) {
        bool canConnect = obs_service_can_try_to_connect(currentService);
        blog(LOG_INFO, "[MultiStream] startPlatformOutput: obs_service_can_try_to_connect returned %s", canConnect ? "true" : "false");
    } else {
        blog(LOG_INFO, "[MultiStream] startPlatformOutput: NO SERVICE ON OUTPUT!");
    }
    
    bool success = obs_output_start(out);
    blog(LOG_INFO, "[MultiStream] startPlatformOutput: obs_output_start for %s returned %s. Last error: %s", 
         platName.toUtf8().constData(), 
         success ? "true" : "false",
         obs_output_get_last_error(out) ? obs_output_get_last_error(out) : "None");
}

void PlatformSessionManager::startStream(OAuthClient* client) {
    if (client->getSession().streamEnabled && client->isLoggedIn()) {
        client->prepareStream([this, client](QString url, QString key) {
            if (!obs_frontend_streaming_active()) {
                mainStreamPlatform = client->getPlatform();
                obs_service_t* service = obs_service_create("rtmp_custom", "main_custom_service", nullptr, nullptr);
                if (service) {
                    obs_data_t* settings = obs_data_create();
                    obs_data_set_string(settings, "server", url.toUtf8().constData());
                    obs_data_set_string(settings, "key", key.toUtf8().constData());
                    obs_service_update(service, settings);
                    obs_data_release(settings);
                    
                    obs_frontend_set_streaming_service(service);
                    obs_frontend_save_streaming_service();
                    obs_service_release(service);
                }
                obs_frontend_streaming_start();
            } else {
                if (mainStreamPlatform != client->getPlatform()) {
                    startPlatformOutput(client, url, key);
                }
            }
        });
    }
}

void PlatformSessionManager::stopStream(OAuthClient* client) {
    if (!client) return;
    QString platName = client->getPlatform();
    if (platName == mainStreamPlatform) {
        obs_frontend_streaming_stop();
    } else if (customOutputs.contains(platName)) {
        platformStates[platName] = OutputState::Stopped;
        obs_output_stop(customOutputs[platName]);
        obs_output_release(customOutputs[platName]);
        customOutputs.remove(platName);
    }
}

bool PlatformSessionManager::isPlatformActive(const QString& platformName) {
    return getPlatformState(platformName) > 0;
}

int PlatformSessionManager::getPlatformState(const QString& platformName) {
    if (platformStates.contains(platformName)) {
        OutputState state = platformStates[platformName];
        if (state == OutputState::Starting || state == OutputState::Reconnecting) {
            return 1;
        } else if (state == OutputState::Active) {
            return 2;
        }
    }
    return 0;
}

void PlatformSessionManager::startCustomStreams() {
    if (twitchClient->getSession().streamEnabled && twitchClient->isLoggedIn()) {
        twitchClient->prepareStream([this](QString url, QString key) {
            startPlatformOutput(twitchClient, url, key);
        });
    }
    if (youtubeClient->getSession().streamEnabled && youtubeClient->isLoggedIn()) {
        youtubeClient->prepareStream([this](QString url, QString key) {
            startPlatformOutput(youtubeClient, url, key);
        });
    }
    if (facebookClient->getSession().streamEnabled && facebookClient->isLoggedIn()) {
        facebookClient->prepareStream([this](QString url, QString key) {
            startPlatformOutput(facebookClient, url, key);
        });
    }
}

void PlatformSessionManager::stopCustomStreams() {
    for (auto it = customOutputs.begin(); it != customOutputs.end(); ++it) {
        obs_output_stop(it.value());
        obs_output_release(it.value());
    }
    customOutputs.clear();
    
    // Disable streamEnabled for all platforms when main stream stops
    auto disableClient = [](OAuthClient* c) {
        PlatformSession s = c->getSession();
        if (s.streamEnabled) {
            s.streamEnabled = false;
            c->updateSession(s);
            c->saveTokens();
        }
    };
    disableClient(twitchClient);
    disableClient(youtubeClient);
    disableClient(facebookClient);
}
PlatformSessionManager::~PlatformSessionManager() {}

void PlatformSessionManager::restoreAllSessions() {
    twitchClient->restoreSession();
    youtubeClient->restoreSession();
    facebookClient->restoreSession();
}
void PlatformSessionManager::saveAllSessions() {
    twitchClient->saveTokens();
    youtubeClient->saveTokens();
    facebookClient->saveTokens();
}

// TwitchOAuthClient
TwitchOAuthClient::TwitchOAuthClient(QObject* parent) : OAuthClient("Twitch", parent) {
    clientId = "y09pvwkyxsfhyy1c9ik8r5g5dgeovs";
    clientSecret = "pb8trwci3zkatn9nbodzra27j2j1ou";
    restoreSession();
}
QString TwitchOAuthClient::getAuthUrl() const { return QString("https://id.twitch.tv/oauth2/authorize?client_id=%1&redirect_uri=http://localhost:8080/&response_type=code&scope=user:read:email+channel:read:stream_key+channel:manage:broadcast").arg(clientId); }
QString TwitchOAuthClient::getTokenUrl() const { return "https://id.twitch.tv/oauth2/token"; }
QUrlQuery TwitchOAuthClient::getTokenRequestParams(const QString& code) const {
    QUrlQuery params;
    params.addQueryItem("client_id", clientId);
    params.addQueryItem("client_secret", clientSecret);
    params.addQueryItem("code", code);
    params.addQueryItem("grant_type", "authorization_code");
    params.addQueryItem("redirect_uri", "http://localhost:8080/");
    return params;
}
void TwitchOAuthClient::fetchProfile() {
    QNetworkRequest request(QUrl("https://api.twitch.tv/helix/users"));
    request.setRawHeader("Client-Id", clientId.toUtf8());
    request.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
    QNetworkReply* reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.contains("data") && obj["data"].toArray().size() > 0) {
                QJsonObject user = obj["data"].toArray()[0].toObject();
                session.channelId = user["id"].toString();
                session.userName = user["display_name"].toString();
                session.email = user["email"].toString();
                session.profilePicture = user["profile_image_url"].toString();
                saveTokens();
                emit profileFetched();
                emit sessionUpdated();
            }
        }
        reply->deleteLater();
    });
}
void TwitchOAuthClient::refreshAccessToken() {
    QNetworkRequest request{QUrl(getTokenUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery params;
    params.addQueryItem("client_id", clientId);
    params.addQueryItem("client_secret", clientSecret);
    params.addQueryItem("refresh_token", session.refreshToken);
    params.addQueryItem("grant_type", "refresh_token");
    QNetworkReply* reply = networkManager->post(request, params.toString().toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            session.accessToken = obj["access_token"].toString();
            if (obj.contains("refresh_token")) session.refreshToken = obj["refresh_token"].toString();
            saveTokens();
            fetchProfile();
        } else {
            logout();
        }
        reply->deleteLater();
    });
}

void TwitchOAuthClient::prepareStream(std::function<void(QString, QString)> callback) {
    if (!session.loggedIn) return;
    
    // First, try to search for the category ID if they provided a category
    if (!session.streamCategory.isEmpty()) {
        QNetworkRequest searchReq(QUrl(QString("https://api.twitch.tv/helix/search/categories?query=%1").arg(QString(QUrl::toPercentEncoding(session.streamCategory)))));
        searchReq.setRawHeader("Client-Id", clientId.toUtf8());
        searchReq.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
        
        QNetworkReply* searchRep = networkManager->get(searchReq);
        connect(searchRep, &QNetworkReply::finished, this, [this, searchRep]() {
            QString gameId;
            if (searchRep->error() == QNetworkReply::NoError) {
                QJsonObject obj = QJsonDocument::fromJson(searchRep->readAll()).object();
                if (obj.contains("data") && obj["data"].toArray().size() > 0) {
                    gameId = obj["data"].toArray()[0].toObject()["id"].toString();
                }
            }
            searchRep->deleteLater();
            
            QNetworkRequest updateReq(QUrl(QString("https://api.twitch.tv/helix/channels?broadcaster_id=%1").arg(session.channelId)));
            updateReq.setRawHeader("Client-Id", clientId.toUtf8());
            updateReq.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
            updateReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            
            QJsonObject updateData;
            updateData["title"] = session.streamTitle;
            if (!gameId.isEmpty()) updateData["game_id"] = gameId;
            
            QNetworkReply* updateRep = networkManager->sendCustomRequest(updateReq, "PATCH", QJsonDocument(updateData).toJson());
            connect(updateRep, &QNetworkReply::finished, updateRep, &QObject::deleteLater);
        });
    } else {
        QNetworkRequest updateReq(QUrl(QString("https://api.twitch.tv/helix/channels?broadcaster_id=%1").arg(session.channelId)));
        updateReq.setRawHeader("Client-Id", clientId.toUtf8());
        updateReq.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
        updateReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QJsonObject updateData;
        updateData["title"] = session.streamTitle;
        QNetworkReply* updateRep = networkManager->sendCustomRequest(updateReq, "PATCH", QJsonDocument(updateData).toJson());
        connect(updateRep, &QNetworkReply::finished, updateRep, &QObject::deleteLater);
    }
    
    QNetworkRequest request(QUrl(QString("https://api.twitch.tv/helix/streams/key?broadcaster_id=%1").arg(session.channelId)));
    request.setRawHeader("Client-Id", clientId.toUtf8());
    request.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
    
    QNetworkReply* reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        QString key;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.contains("data") && obj["data"].toArray().size() > 0) {
                key = obj["data"].toArray()[0].toObject()["stream_key"].toString();
            }
        }
        reply->deleteLater();
        if (!key.isEmpty()) {
            callback("rtmp://live.twitch.tv/app", key);
        }
    });
}

// YouTubeOAuthClient
YouTubeOAuthClient::YouTubeOAuthClient(QObject* parent) : OAuthClient("YouTube", parent) {
    clientId = "228735003663-5qcl245j7uelsfpbr97hpgjbo2ra8grm.apps.googleusercontent.com";
    clientSecret = "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";
    restoreSession();
}
QString YouTubeOAuthClient::getAuthUrl() const { return QString("https://accounts.google.com/o/oauth2/v2/auth?client_id=%1&redirect_uri=http://localhost:8080/&response_type=code&scope=https://www.googleapis.com/auth/youtube.readonly+https://www.googleapis.com/auth/youtube").arg(clientId); }
QString YouTubeOAuthClient::getTokenUrl() const { return "https://oauth2.googleapis.com/token"; }
QUrlQuery YouTubeOAuthClient::getTokenRequestParams(const QString& code) const {
    QUrlQuery params;
    params.addQueryItem("client_id", clientId);
    params.addQueryItem("client_secret", clientSecret);
    params.addQueryItem("code", code);
    params.addQueryItem("grant_type", "authorization_code");
    params.addQueryItem("redirect_uri", "http://localhost:8080/");
    return params;
}
void YouTubeOAuthClient::fetchProfile() {
    QNetworkRequest request(QUrl("https://www.googleapis.com/youtube/v3/channels?part=snippet,statistics&mine=true"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
    QNetworkReply* reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.contains("items") && obj["items"].toArray().size() > 0) {
                QJsonObject item = obj["items"].toArray()[0].toObject();
                QJsonObject snippet = item["snippet"].toObject();
                QJsonObject stats = item["statistics"].toObject();
                session.channelId = item["id"].toString();
                session.userName = snippet["title"].toString();
                if (snippet.contains("thumbnails") && snippet["thumbnails"].toObject().contains("default")) {
                    session.profilePicture = snippet["thumbnails"].toObject()["default"].toObject()["url"].toString();
                }
                session.extraData = stats["subscriberCount"].toString();
                saveTokens();
                emit profileFetched();
                emit sessionUpdated();
            }
        }
        reply->deleteLater();
    });
}
void YouTubeOAuthClient::refreshAccessToken() {
    QNetworkRequest request{QUrl(getTokenUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery params;
    params.addQueryItem("client_id", clientId);
    params.addQueryItem("client_secret", clientSecret);
    params.addQueryItem("refresh_token", session.refreshToken);
    params.addQueryItem("grant_type", "refresh_token");
    QNetworkReply* reply = networkManager->post(request, params.toString().toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            session.accessToken = obj["access_token"].toString();
            saveTokens();
            fetchProfile();
        } else {
            logout();
        }
        reply->deleteLater();
    });
}

void YouTubeOAuthClient::prepareStream(std::function<void(QString, QString)> callback) {
    if (!session.loggedIn) return;

    QNetworkRequest req(QUrl("https://youtube.googleapis.com/youtube/v3/liveBroadcasts?part=snippet,status,contentDetails&broadcastType=all&mine=true"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());

    QNetworkReply* rep = networkManager->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, callback]() {
        QString boundStreamId;
        if (rep->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(rep->readAll()).object();
            if (obj.contains("items") && obj["items"].toArray().size() > 0) {
                QJsonArray items = obj["items"].toArray();
                QJsonObject broadcast;
                bool found = false;
                
                for (int i = 0; i < items.size(); ++i) {
                    QJsonObject b = items[i].toObject();
                    QString st = b["status"].toObject()["lifeCycleStatus"].toString();
                    if (st == "live" || st == "testing") {
                        broadcast = b; found = true; break;
                    }
                }
                if (!found) {
                    for (int i = 0; i < items.size(); ++i) {
                        QJsonObject b = items[i].toObject();
                        QString st = b["status"].toObject()["lifeCycleStatus"].toString();
                        if (st == "ready" || st == "created") {
                            broadcast = b; found = true; break;
                        }
                    }
                }
                if (!found) {
                    broadcast = items[0].toObject();
                }

                QString broadcastId = broadcast["id"].toString();
                if (broadcast.contains("contentDetails")) {
                    boundStreamId = broadcast["contentDetails"].toObject()["boundStreamId"].toString();
                }

                QJsonObject snippet = broadcast["snippet"].toObject();
                QJsonObject status = broadcast["status"].toObject();
                
                QString title = session.streamTitle.trimmed();
                if (title.isEmpty()) title = "-";
                QString description = session.streamDescription.trimmed();
                if (description.isEmpty()) description = "-";
                
                snippet["title"] = title;
                snippet["description"] = description;
                
                QString cat = session.streamCategory.toLower();
                QString catId = "22"; // People & Blogs default
                if (cat == "gaming") catId = "20";
                else if (cat == "education") catId = "27";
                else if (cat == "entertainment") catId = "24";
                else if (cat == "music") catId = "10";
                else if (cat == "sports") catId = "17";
                else if (cat == "comedy") catId = "23";
                else if (cat == "news & politics" || cat == "news") catId = "25";
                
                // Setting categoryId on broadcast snippet is deprecated but harmless
                snippet["categoryId"] = catId;
                
                QString privacy = session.streamPrivacy.toLower();
                if (privacy.isEmpty()) privacy = "public";
                status["privacyStatus"] = privacy;
                
                QJsonObject updateData;
                updateData["id"] = broadcastId;
                updateData["snippet"] = snippet;
                updateData["status"] = status;
                
                QNetworkRequest updateReq(QUrl("https://youtube.googleapis.com/youtube/v3/liveBroadcasts?part=snippet,status"));
                updateReq.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
                updateReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                
                QNetworkReply* updateRep = networkManager->put(updateReq, QJsonDocument(updateData).toJson());
                connect(updateRep, &QNetworkReply::finished, updateRep, [updateRep]() {
                    if (updateRep->error() != QNetworkReply::NoError) {
                        qDebug() << "YouTube API Update Error:" << updateRep->readAll();
                    }
                    updateRep->deleteLater();
                });

                // Background update for video category (as required by YouTube API for categories)
                QNetworkRequest vidReq(QUrl("https://youtube.googleapis.com/youtube/v3/videos?part=snippet&id=" + broadcastId));
                vidReq.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
                QNetworkReply* vidRep = networkManager->get(vidReq);
                connect(vidRep, &QNetworkReply::finished, vidRep, [this, vidRep, broadcastId, catId]() {
                    if (vidRep->error() == QNetworkReply::NoError) {
                        QJsonObject vObj = QJsonDocument::fromJson(vidRep->readAll()).object();
                        if (vObj.contains("items") && vObj["items"].toArray().size() > 0) {
                            QJsonObject video = vObj["items"].toArray()[0].toObject();
                            QJsonObject vSnippet = video["snippet"].toObject();
                            vSnippet["categoryId"] = catId;
                            
                            QJsonObject vUpdateData;
                            vUpdateData["id"] = broadcastId;
                            vUpdateData["snippet"] = vSnippet;
                            
                            QNetworkRequest vUpReq(QUrl("https://youtube.googleapis.com/youtube/v3/videos?part=snippet"));
                            vUpReq.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
                            vUpReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                            
                            QNetworkReply* vUpRep = networkManager->put(vUpReq, QJsonDocument(vUpdateData).toJson());
                            connect(vUpRep, &QNetworkReply::finished, vUpRep, &QObject::deleteLater);
                        }
                    }
                    vidRep->deleteLater();
                });
            }
        }
        rep->deleteLater();

        QString streamReqUrl = "https://youtube.googleapis.com/youtube/v3/liveStreams?part=cdn&mine=true";
        if (!boundStreamId.isEmpty()) {
            streamReqUrl = "https://youtube.googleapis.com/youtube/v3/liveStreams?part=cdn&id=" + boundStreamId;
        }
        QNetworkRequest request{QUrl(streamReqUrl)};
        request.setRawHeader("Authorization", QString("Bearer %1").arg(session.accessToken).toUtf8());
        QNetworkReply* reply = networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
            QString url, key;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonObject obj2 = QJsonDocument::fromJson(reply->readAll()).object();
                if (obj2.contains("items") && obj2["items"].toArray().size() > 0) {
                    QJsonObject cdn = obj2["items"].toArray()[0].toObject()["cdn"].toObject();
                    key = cdn["ingestionInfo"].toObject()["streamName"].toString();
                    url = cdn["ingestionInfo"].toObject()["ingestionAddress"].toString();
                }
            }
            reply->deleteLater();
            if (!key.isEmpty()) callback(url, key);
        });
    });
}

// FacebookOAuthClient
FacebookOAuthClient::FacebookOAuthClient(QObject* parent) : OAuthClient("Facebook", parent) {
    clientId = "4449848045251374";
    clientSecret = "eb77d884343d09d4eb144a067914525b";
    restoreSession();
}
QString FacebookOAuthClient::getAuthUrl() const { return QString("https://www.facebook.com/v17.0/dialog/oauth?client_id=%1&redirect_uri=http://localhost:8080/&response_type=code&scope=email,pages_show_list,pages_read_engagement,pages_manage_posts,publish_video").arg(clientId); }
QString FacebookOAuthClient::getTokenUrl() const { return "https://graph.facebook.com/v17.0/oauth/access_token"; }
QUrlQuery FacebookOAuthClient::getTokenRequestParams(const QString& code) const {
    QUrlQuery params;
    params.addQueryItem("client_id", clientId);
    params.addQueryItem("client_secret", clientSecret);
    params.addQueryItem("code", code);
    params.addQueryItem("redirect_uri", "http://localhost:8080/");
    return params;
}
void FacebookOAuthClient::fetchProfile() {
    // 1. Fetch User Profile
    QNetworkRequest request(QUrl(QString("https://graph.facebook.com/me?fields=id,name,email,picture&access_token=%1").arg(session.accessToken)));
    QNetworkReply* reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            session.channelId = obj["id"].toString();
            session.userName = obj["name"].toString();
            session.email = obj["email"].toString();
            if (obj.contains("picture") && obj["picture"].toObject().contains("data")) {
                session.profilePicture = obj["picture"].toObject()["data"].toObject()["url"].toString();
            }
            // 2. Fetch Pages
            QNetworkRequest pageReq(QUrl(QString("https://graph.facebook.com/me/accounts?access_token=%1").arg(session.accessToken)));
            QNetworkReply* pageReply = networkManager->get(pageReq);
            connect(pageReply, &QNetworkReply::finished, this, [this, pageReply]() {
                if (pageReply->error() == QNetworkReply::NoError) {
                    QJsonObject pObj = QJsonDocument::fromJson(pageReply->readAll()).object();
                    if (pObj.contains("data")) {
                        QJsonArray data = pObj["data"].toArray();
                        QStringList pagesList;
                        for (int i=0; i < data.size(); i++) {
                            QJsonObject page = data[i].toObject();
                            pagesList << page["name"].toString() + "|" + page["id"].toString() + "|" + page["access_token"].toString();
                        }
                        session.extraData = pagesList.join(";;");
                    }
                }
                saveTokens();
                emit profileFetched();
                emit sessionUpdated();
                pageReply->deleteLater();
            });
        }
        reply->deleteLater();
    });
}
void FacebookOAuthClient::refreshAccessToken() {
    // Facebook has long-lived tokens rather than refresh_token grants
    logout(); 
}

void FacebookOAuthClient::prepareStream(std::function<void(QString, QString)> callback) {
    if (!session.loggedIn) return;
    
    QNetworkRequest request(QUrl(QString("https://graph.facebook.com/me/live_videos")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QUrlQuery params;
    params.addQueryItem("access_token", session.accessToken);
    params.addQueryItem("status", "LIVE_NOW");
    if (!session.streamTitle.isEmpty()) params.addQueryItem("title", session.streamTitle);
    if (!session.streamDescription.isEmpty()) params.addQueryItem("description", session.streamDescription);
    
    QString privacy = session.streamPrivacy.toLower();
    QString fbPrivacy = "{\"value\":\"EVERYONE\"}"; // default public
    if (privacy == "private" || privacy == "unlisted") {
        fbPrivacy = "{\"value\":\"SELF\"}";
    }
    params.addQueryItem("privacy", fbPrivacy);
    
    QNetworkReply* reply = networkManager->post(request, params.toString().toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        QString url;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            url = obj["stream_url"].toString(); 
        }
        reply->deleteLater();
        if (!url.isEmpty()) callback(url, "");
    });
}
