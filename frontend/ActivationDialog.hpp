#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class ActivationDialog : public QDialog {
    Q_OBJECT

public:
    explicit ActivationDialog(const QString &reason, QWidget *parent = nullptr);
    ~ActivationDialog();

private slots:
    void onRefreshClicked();

private:
    QLabel *messageLabel;
    QPushButton *refreshButton;
    QPushButton *exitButton;
    QLabel *iconLabel;
};
