#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

class LicenseManager : public QObject {
	Q_OBJECT

public:
	static LicenseManager *instance();

	// Returns true if license is active (or offline grace period is valid)
	// Returns false if expired, blocked, or offline grace period exceeded.
	// If false, outErrorReason will contain the message to display.
	bool ValidateOnStartup(QString &outErrorReason);

	// Starts a QTimer to validate every 5 minutes
	void StartPeriodicValidation();

	// Check if the license is currently active based on the local cache
	bool IsLicenseActive(QString *outErrorReason = nullptr);

	// Get the cached license expiry date
	QString GetLicenseExpiry();

	// Get the cached user name
	QString GetCachedUserName();

	// Get the cached user email
	QString GetCachedUserEmail();
signals:
	// Emitted when the periodic background check fails
	void licenseInvalidated(const QString &reason);
	
	// Emitted when the periodic background check succeeds
	void licenseValidated();

public slots:
	void performValidation();

private:
	LicenseManager(QObject *parent = nullptr);
	~LicenseManager();

	bool readLocalCache(QString &outErrorReason);
	void saveLocalCache(const QString &status, const QString &expiry);
	QString generateSignature(const QString &status, const QString &expiry, qint64 lastCheck) const;
	QString getMachineId() const;

	QNetworkAccessManager *networkManager;
	QTimer *validationTimer;
};
