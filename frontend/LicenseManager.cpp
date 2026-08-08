#include "LicenseManager.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QSysInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <obs.h>
#include "OBSApp.hpp"

#define API_URL "https://techrise-api.onrender.com/v1/verify-license"
#define SECRET_SALT "TechrisSuperSecretSalt2026!"
#define OFFLINE_GRACE_PERIOD_DAYS 3
#define PERIODIC_CHECK_MS (30 * 1000)

LicenseManager *LicenseManager::instance()
{
	static LicenseManager inst;
	return &inst;
}

LicenseManager::LicenseManager(QObject *parent) : QObject(parent)
{
	networkManager = new QNetworkAccessManager(this);
	validationTimer = new QTimer(this);
	validationTimer->setInterval(PERIODIC_CHECK_MS);
	connect(validationTimer, &QTimer::timeout, this, &LicenseManager::performValidation);
}

LicenseManager::~LicenseManager() {}

QString LicenseManager::getMachineId() const
{
	QString machineId = QString::fromUtf8(QSysInfo::machineUniqueId());
	if (machineId.isEmpty()) {
		machineId = "UNKNOWN_MACHINE_ID";
	}
	return machineId;
}

QString LicenseManager::generateSignature(const QString &status, const QString &expiry, qint64 lastCheck) const
{
	QString data = status + "|" + expiry + "|" + QString::number(lastCheck) + "|" + getMachineId() + "|" + SECRET_SALT;
	QByteArray hash = QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256);
	return QString::fromUtf8(hash.toHex());
}

void LicenseManager::saveLocalCache(const QString &status, const QString &expiry)
{
	QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/obs-studio";
	QDir dir(path);
	if (!dir.exists()) {
		dir.mkpath(".");
	}
	QFile file(path + "/license_cache.json");
	if (file.open(QIODevice::WriteOnly)) {
		qint64 now = QDateTime::currentSecsSinceEpoch();
		QJsonObject obj;
		obj["status"] = status;
		obj["expiry_date"] = expiry;
		obj["last_check"] = now;
		obj["signature"] = generateSignature(status, expiry, now);

		QJsonDocument doc(obj);
		file.write(doc.toJson());
		file.close();
	}
}

bool LicenseManager::readLocalCache(QString &outErrorReason)
{
	QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/obs-studio/license_cache.json";
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		outErrorReason = "Cannot connect to the server to verify your license, and no local offline cache was found. Please connect to the internet.";
		return false;
	}

	QByteArray data = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (doc.isNull() || !doc.isObject()) {
		outErrorReason = "Corrupted offline license cache. Please connect to the internet.";
		return false;
	}

	QJsonObject obj = doc.object();
	QString status = obj["status"].toString();
	QString expiry = obj["expiry_date"].toString();
	qint64 lastCheck = obj["last_check"].toVariant().toLongLong();
	QString signature = obj["signature"].toString();

	QString expectedSignature = generateSignature(status, expiry, lastCheck);
	if (signature != expectedSignature) {
		outErrorReason = "Invalid offline license signature. Please connect to the internet.";
		return false;
	}

	qint64 now = QDateTime::currentSecsSinceEpoch();
	qint64 diff = now - lastCheck;
	if (diff > OFFLINE_GRACE_PERIOD_DAYS * 24 * 60 * 60) {
		outErrorReason = "Offline grace period expired. Please connect to the internet to verify your license.";
		return false;
	}

	if (status.toUpper() == "BLOCKED" || status.toUpper() == "SUSPENDED" || status.toUpper() == "BLOCKED_BY_ADMIN") {
		outErrorReason = "Your account is blocked by the Techrise admin.";
		return false;
	}
	if (status.toUpper() == "EXPIRED") {
		outErrorReason = "Your Techrise subscription has expired. Please renew your license.";
		return false;
	}
	if (status.toUpper() != "ACTIVE") {
		outErrorReason = "You need to purchase a Pro subscription.";
		return false;
	}

	return true; // ACTIVE and within grace period
}

bool LicenseManager::ValidateOnStartup(QString &outErrorReason)
{
	QNetworkRequest request(QUrl(API_URL));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QJsonObject payload;
	const char *savedUserId = config_get_string(App()->GetAppConfig(), "General", "UserId");
	payload["user_id"] = (savedUserId && strlen(savedUserId) > 0) ? QString(savedUserId) : "local_user"; 
	payload["device_id"] = getMachineId();
	payload["software_version"] = QString(obs_get_version_string());
	
	QJsonDocument doc(payload);
	QByteArray postData = doc.toJson();

	QNetworkReply *reply = networkManager->post(request, postData);
	
	QEventLoop loop;
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	
	// Set a timeout of 10 seconds for startup verification
	QTimer timeoutTimer;
	timeoutTimer.setSingleShot(true);
	connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeoutTimer.start(10000);
	
	loop.exec();

	if (reply->isRunning()) {
		// Timeout
		reply->abort();
		reply->deleteLater();
		return readLocalCache(outErrorReason);
	}

	QByteArray responseData = reply->readAll();
	reply->deleteLater();

	QJsonDocument resDoc = QJsonDocument::fromJson(responseData);
	int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (!resDoc.isObject()) {
		// If there's an error and no JSON, fallback to cache unless it's a 4xx error
		if (statusCode >= 400 && statusCode < 500) {
			outErrorReason = "You need to purchase a Pro subscription.";
			saveLocalCache("UNSUBSCRIBED", "");
			return false;
		}
		return readLocalCache(outErrorReason);
	}

	QJsonObject resObj = resDoc.object();
	QString status = resObj["status"].toString("INACTIVE"); // Default to INACTIVE
	QString expiry = resObj["expiry_date"].toString("");

	saveLocalCache(status, expiry);

	if (status.toUpper() == "BLOCKED" || status.toUpper() == "SUSPENDED" || status.toUpper() == "BLOCKED_BY_ADMIN") {
		outErrorReason = "Your account is blocked by the Techrise admin.";
		return false;
	}
	if (status.toUpper() == "EXPIRED") {
		outErrorReason = "Your Techrise subscription has expired. Please renew your license.";
		return false;
	}
	if (status.toUpper() != "ACTIVE") {
		outErrorReason = "You need to purchase a Pro subscription.";
		return false;
	}

	return true;
}

void LicenseManager::StartPeriodicValidation()
{
	if (!validationTimer->isActive()) {
		validationTimer->start();
		QTimer::singleShot(0, this, &LicenseManager::performValidation);
	}
}

void LicenseManager::performValidation()
{
	QNetworkRequest request(QUrl(API_URL));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
	request.setTransferTimeout(10000); // 10 second timeout
#endif

	QJsonObject payload;
	const char *savedUserId = config_get_string(App()->GetAppConfig(), "General", "UserId");
	payload["user_id"] = (savedUserId && strlen(savedUserId) > 0) ? QString(savedUserId) : "local_user"; 
	payload["device_id"] = getMachineId();
	payload["software_version"] = QString(obs_get_version_string());
	
	QJsonDocument doc(payload);
	QByteArray postData = doc.toJson();

	QNetworkReply *reply = networkManager->post(request, postData);
	
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		QByteArray responseData = reply->readAll();
		QJsonDocument resDoc = QJsonDocument::fromJson(responseData);
		
		if (resDoc.isObject()) {
			QJsonObject resObj = resDoc.object();
			QString status = resObj["status"].toString("INACTIVE");
			QString expiry = resObj["expiry_date"].toString("");
			
			saveLocalCache(status, expiry);
			
			if (status.toUpper() == "ACTIVE") {
				emit licenseValidated();
			} else if (status.toUpper() == "BLOCKED" || status.toUpper() == "BLOCKED_BY_ADMIN" || status.toUpper() == "SUSPENDED") {
				emit licenseInvalidated("Your account is blocked by the Techrise admin.");
			} else if (status.toUpper() == "EXPIRED") {
				emit licenseInvalidated("Your Techrise subscription has expired. Please renew your license.");
			} else {
				emit licenseInvalidated("You need to purchase a Pro subscription.");
			}
		} else {
			// If we got an error and no valid JSON, fallback to local cache
			int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (statusCode >= 400 && statusCode < 500) {
				saveLocalCache("UNSUBSCRIBED", "");
				emit licenseInvalidated("You need to purchase a Pro subscription.");
			} else {
				QString errorReason;
				if (!readLocalCache(errorReason)) {
					emit licenseInvalidated(errorReason);
				}
			}
		}
	});
}

bool LicenseManager::IsLicenseActive(QString *outErrorReason)
{
	QString errorReason;
	bool active = readLocalCache(errorReason);
	if (outErrorReason) {
		*outErrorReason = errorReason;
	}
	return active;
}

QString LicenseManager::GetLicenseExpiry()
{
	QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/obs-studio/license_cache.json";
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return "Unknown";
	}
	QByteArray data = file.readAll();
	file.close();
	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (doc.isNull() || !doc.isObject()) {
		return "Unknown";
	}
	return doc.object()["expiry_date"].toString();
}

QString LicenseManager::GetCachedUserName()
{
	if (App() && App()->GetAppConfig()) {
		const char* name = config_get_string(App()->GetAppConfig(), "General", "UserName");
		if (name && strlen(name) > 0) return QString(name);
	}

	QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/obs-studio/license_cache.json";
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return "TechRise User";
	}
	QByteArray data = file.readAll();
	file.close();
	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (doc.isNull() || !doc.isObject()) {
		return "TechRise User";
	}
	QString name = doc.object()["user_name"].toString();
	return name.isEmpty() ? "TechRise User" : name;
}

QString LicenseManager::GetCachedUserEmail()
{
	if (App() && App()->GetAppConfig()) {
		const char* email = config_get_string(App()->GetAppConfig(), "General", "UserEmail");
		if (email && strlen(email) > 0) return QString(email);
	}

	QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/obs-studio/license_cache.json";
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return "user@techrise.com";
	}
	QByteArray data = file.readAll();
	file.close();
	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (doc.isNull() || !doc.isObject()) {
		return "user@techrise.com";
	}
	QString email = doc.object()["user_email"].toString();
	return email.isEmpty() ? "user@techrise.com" : email;
}
