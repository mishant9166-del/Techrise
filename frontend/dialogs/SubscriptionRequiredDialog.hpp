#pragma once

#include <QDialog>
#include <QString>

class SubscriptionRequiredDialog : public QDialog {
	Q_OBJECT

public:
	explicit SubscriptionRequiredDialog(const QString &reason, QWidget *parent = nullptr);
	~SubscriptionRequiredDialog() override;
};

