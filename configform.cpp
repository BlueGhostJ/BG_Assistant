#include "configform.h"
#include "ui_configform.h"

#include <QSettings>
#include <QCoreApplication>
#include <QMessageBox>

ConfigForm::ConfigForm(QDialog *parent)
    : QDialog(parent)
    , ui(new Ui::ConfigForm)
{
    ui->setupUi(this);
    this->setWindowIcon(QIcon(QString(":/icon/icons/config.png"))); // 标题栏icon
    this->setAttribute(Qt::WA_QuitOnClose, false); // 关闭窗口不退出
    loadSettings();
}

ConfigForm::~ConfigForm()
{
    delete ui;
}


void setAutoStart(bool enable) {
    QSettings settings(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);
    QString appName = QCoreApplication::applicationName();

    if (enable) {
        QString appPath = QCoreApplication::applicationFilePath();
        appPath = "\"" + appPath.replace("/", "\\") + "\"";
        settings.setValue(appName, appPath);
    } else {
        settings.remove(appName);
    }
}


bool isAutoStartEnabled() {
    QSettings settings(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);
    QString appName = QCoreApplication::applicationName();

    QString appPath = QCoreApplication::applicationFilePath();
    appPath = "\"" + appPath.replace("/", "\\") + "\"";
    return settings.contains(appName) && settings.value(appName) == appPath;
}


void ConfigForm::loadSettings() {
    ui->autoStartCheckBox->setChecked(isAutoStartEnabled());
}

void ConfigForm::on_autoStartCheckBox_stateChanged(int state) {
    setAutoStart(state == Qt::Checked);
}

