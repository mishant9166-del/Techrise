#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class AccountSuspendedDialog : public QDialog {
	Q_OBJECT

public:
	AccountSuspendedDialog(QWidget *parent = nullptr);
};
