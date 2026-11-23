#include "chromeprofileform.h"
#include "ui_chromeprofileform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>

namespace {

// 从 bat 里复制过来的参数常带引号，保存前去掉
QString stripped(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
        text = text.mid(1, text.size() - 2).trimmed();
    return text;
}

} // namespace

ChromeProfileForm::ChromeProfileForm(const QList<ChromeProfile> &others, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChromeProfileForm)
    , m_others(others)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));

    // 输入框吃掉多余宽度，标签列保持原宽
    ui->basicLayout->setColumnStretch(1, 1);
    ui->networkLayout->setColumnStretch(1, 1);
    ui->launchLayout->setColumnStretch(1, 1);

    // 回车默认走「确定」，但「取消」不抢默认按钮
    ui->okButton->setAutoDefault(true);
    ui->cancelButton->setAutoDefault(false);

    connect(ui->okButton, &QPushButton::clicked, this, &ChromeProfileForm::submit);
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // 勾选框统一驱动「对应输入框是否可用」+ 命令预览
    const QList<QCheckBox *> optionChecks{
        ui->userAgentCheckBox, ui->appModeCheckBox,    ui->windowSizeCheckBox,
        ui->windowPositionCheckBox, ui->windowNameCheckBox, ui->maximizedCheckBox,
        ui->fullscreenCheckBox, ui->kioskCheckBox,     ui->scale150CheckBox,
    };
    for (QCheckBox *check : optionChecks) {
        connect(check, &QCheckBox::toggled, this, [this]() {
            updateEnabledStates();
            updatePreview();
        });
    }

    // 最大化 / 强制全屏 / 演示在 Chrome 里互相覆盖，勾一个就取消另外两个
    connect(ui->maximizedCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked)
            return;
        ui->fullscreenCheckBox->setChecked(false);
        ui->kioskCheckBox->setChecked(false);
    });
    connect(ui->fullscreenCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked)
            return;
        ui->maximizedCheckBox->setChecked(false);
        ui->kioskCheckBox->setChecked(false);
    });
    connect(ui->kioskCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked)
            return;
        ui->maximizedCheckBox->setChecked(false);
        ui->fullscreenCheckBox->setChecked(false);
    });

    connect(ui->nameEdit, &QLineEdit::textChanged, this, [this]() {
        updateNameHint();
        updatePreview();
    });
    connect(ui->proxyServerEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(ui->userAgentEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(ui->appUrlEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(ui->windowNameEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });

    connect(ui->proxyModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        updateEnabledStates();
        updatePreview();
    });

    const QList<QSpinBox *> spins{ui->windowWidthSpin, ui->windowHeightSpin,
                                  ui->windowXSpin, ui->windowYSpin};
    for (QSpinBox *spin : spins) {
        connect(spin, &QSpinBox::valueChanged, this, [this]() { updatePreview(); });
    }

    setProfile(ChromeProfile());
}

ChromeProfileForm::~ChromeProfileForm()
{
    delete ui;
}

void ChromeProfileForm::setProfile(const ChromeProfile &profile)
{
    m_originalName = profile.name;

    ui->nameEdit->setText(profile.name);
    ui->proxyModeCombo->setCurrentIndex(profile.proxyMode);   // 顺序与 ProxyMode 枚举一致
    ui->proxyServerEdit->setText(profile.proxyServer);

    ui->userAgentCheckBox->setChecked(profile.useUserAgent);
    ui->userAgentEdit->setText(profile.userAgent);

    ui->appModeCheckBox->setChecked(profile.useAppMode);
    ui->appUrlEdit->setText(profile.appUrl);

    ui->windowSizeCheckBox->setChecked(profile.useWindowSize);
    ui->windowWidthSpin->setValue(profile.windowWidth);
    ui->windowHeightSpin->setValue(profile.windowHeight);

    ui->windowPositionCheckBox->setChecked(profile.useWindowPosition);
    ui->windowXSpin->setValue(profile.windowX);
    ui->windowYSpin->setValue(profile.windowY);

    ui->windowNameCheckBox->setChecked(profile.useWindowName);
    ui->windowNameEdit->setText(profile.windowName);

    ui->scale150CheckBox->setChecked(profile.forceScale150);

    // 三个开窗方式互斥：按「最大化 → 全屏 → 演示」的顺序勾选，
    // 后勾的会取消前面的，最终与 optionSummary/buildArguments 的优先级一致。
    ui->maximizedCheckBox->setChecked(profile.startMaximized);
    ui->fullscreenCheckBox->setChecked(profile.startFullscreen);
    ui->kioskCheckBox->setChecked(profile.kiosk);

    setWindowTitle(m_originalName.isEmpty()
                       ? QStringLiteral("新建 Chrome 多开")
                       : QStringLiteral("编辑 Chrome 多开 - %1").arg(m_originalName));

    updateEnabledStates();
    updateNameHint();
    updatePreview();
}

ChromeProfile ChromeProfileForm::profile() const
{
    ChromeProfile profile;

    profile.name = ui->nameEdit->text().trimmed();
    profile.proxyMode = ui->proxyModeCombo->currentIndex();
    profile.proxyServer = stripped(ui->proxyServerEdit->text());

    profile.useUserAgent = ui->userAgentCheckBox->isChecked();
    profile.userAgent = stripped(ui->userAgentEdit->text());

    profile.useAppMode = ui->appModeCheckBox->isChecked();
    profile.appUrl = stripped(ui->appUrlEdit->text());

    profile.useWindowSize = ui->windowSizeCheckBox->isChecked();
    profile.windowWidth = ui->windowWidthSpin->value();
    profile.windowHeight = ui->windowHeightSpin->value();

    profile.useWindowPosition = ui->windowPositionCheckBox->isChecked();
    profile.windowX = ui->windowXSpin->value();
    profile.windowY = ui->windowYSpin->value();

    profile.startMaximized = ui->maximizedCheckBox->isChecked();
    profile.startFullscreen = ui->fullscreenCheckBox->isChecked();
    profile.kiosk = ui->kioskCheckBox->isChecked();
    profile.forceScale150 = ui->scale150CheckBox->isChecked();

    profile.useWindowName = ui->windowNameCheckBox->isChecked();
    profile.windowName = stripped(ui->windowNameEdit->text());

    return profile;
}

void ChromeProfileForm::submit()
{
    const QString name = ui->nameEdit->text().trimmed();

    // 必填项：名称
    QString reason;
    if (!ChromeMultiOpen::isValidName(name, &reason)) {
        QMessageBox::warning(this, windowTitle(), reason);
        ui->nameEdit->setFocus();
        ui->nameEdit->selectAll();
        return;
    }

    // 名称同时是数据目录名，重名会让两个多开共用一套登录状态
    for (const ChromeProfile &other : m_others) {
        if (other.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("已经有一个叫「%1」的多开了，请换一个名称。\n\n"
                                            "名称同时是数据目录名，重名会共用同一套登录状态。")
                                 .arg(name));
        ui->nameEdit->setFocus();
        ui->nameEdit->selectAll();
        return;
    }

    // 勾了选项却没填内容，启动时会被当成「没配置」，这里提前挡住
    if (ui->proxyModeCombo->currentIndex() == ChromeProfile::ProxyCustom
        && ui->proxyServerEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("代理方式选了「指定代理服务器」，请填写代理地址。\n\n"
                                            "不需要代理时，请改选「使用系统代理」或「不使用代理」。"));
        ui->proxyServerEdit->setFocus();
        return;
    }
    if (ui->userAgentCheckBox->isChecked() && ui->userAgentEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("勾选了「指定 User-Agent」，请填写要使用的 UA 字符串。"));
        ui->userAgentEdit->setFocus();
        return;
    }
    if (ui->appModeCheckBox->isChecked() && ui->appUrlEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("勾选了「独立窗口模式运行」，请填写要打开的网址。"));
        ui->appUrlEdit->setFocus();
        return;
    }
    if (ui->windowNameCheckBox->isChecked() && ui->windowNameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("勾选了「指定窗口名称」，请填写窗口名称。"));
        ui->windowNameEdit->setFocus();
        return;
    }

    // 名称前后的空格不进入目录名
    if (ui->nameEdit->text() != name)
        ui->nameEdit->setText(name);

    QDialog::accept();
}

void ChromeProfileForm::updateEnabledStates()
{
    // 只有「指定代理服务器」才需要填地址
    ui->proxyServerEdit->setEnabled(ui->proxyModeCombo->currentIndex() == ChromeProfile::ProxyCustom);

    ui->userAgentEdit->setEnabled(ui->userAgentCheckBox->isChecked());
    ui->appUrlEdit->setEnabled(ui->appModeCheckBox->isChecked());
    ui->windowWidthSpin->setEnabled(ui->windowSizeCheckBox->isChecked());
    ui->windowHeightSpin->setEnabled(ui->windowSizeCheckBox->isChecked());
    ui->windowXSpin->setEnabled(ui->windowPositionCheckBox->isChecked());
    ui->windowYSpin->setEnabled(ui->windowPositionCheckBox->isChecked());
    ui->windowNameEdit->setEnabled(ui->windowNameCheckBox->isChecked());
}

void ChromeProfileForm::updateNameHint()
{
    const QString name = ui->nameEdit->text().trimmed();
    if (name.isEmpty()) {
        ui->dataDirLabel->setText(
            QStringLiteral("填写名称后，自动生成「数据位置」下的同名子目录"));
        return;
    }

    QString reason;
    if (!ChromeMultiOpen::isValidName(name, &reason)) {
        ui->dataDirLabel->setText(QStringLiteral("名称不合法：%1").arg(reason));
        return;
    }

    // 目录已经存在时明确提示一下：这正是「指定数据位置后直接沿用已有登录数据」的用法
    const QString dir = ChromeMultiOpen::dataDirFor(name);
    const QString state = QFileInfo(dir).isDir()
                              ? QStringLiteral("目录已存在，将沿用其中的登录状态。")
                              : QStringLiteral("目录尚未创建，首次启动时自动创建。");

    ui->dataDirLabel->setText(QStringLiteral("%1\n%2")
                                  .arg(QDir::toNativeSeparators(dir), state));
}

void ChromeProfileForm::updatePreview()
{
    ui->previewEdit->setText(ChromeMultiOpen::commandLine(profile()));
    ui->previewEdit->setCursorPosition(0);
}
