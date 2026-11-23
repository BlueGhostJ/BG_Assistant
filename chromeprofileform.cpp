#include "chromeprofileform.h"
#include "ui_chromeprofileform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
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

ChromeProfileForm::ChromeProfileForm(Mode mode, const QList<ChromeProfile> &others, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChromeProfileForm)
    , m_others(others)
    , m_mode(mode)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));

    // 新建时不让用户填数据目录：默认就是 <全局位置>\<名称>，创建后记进配置；
    // 需要单独指定目录时再进「编辑」（那里有浏览 / 用全局目录）。
    const bool showDataDir = (m_mode == EditMode);
    ui->dataDirTitleLabel->setVisible(showDataDir);
    ui->dataDirEdit->setVisible(showDataDir);
    ui->browseDataDirButton->setVisible(showDataDir);
    ui->globalDataDirButton->setVisible(showDataDir);
    ui->dataDirHintLabel->setVisible(showDataDir);

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
        ui->noImagesCheckBox,
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
        updateDataDirState();
        updatePreview();
    });
    connect(ui->proxyServerEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(ui->userAgentEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(ui->appUrlEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(ui->windowNameEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });

    connect(ui->proxyModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        // 没选「指定代理服务器」时把地址清空并置灰：避免界面上留着一条用不到的旧值
        if (ui->proxyModeCombo->currentIndex() != ChromeProfile::ProxyCustom)
            ui->proxyServerEdit->clear();
        updateEnabledStates();
        updatePreview();
    });

    const QList<QSpinBox *> spins{ui->windowWidthSpin, ui->windowHeightSpin,
                                  ui->windowXSpin, ui->windowYSpin};
    for (QSpinBox *spin : spins) {
        connect(spin, &QSpinBox::valueChanged, this, [this]() { updatePreview(); });
    }

    // 手动改过目录就不再跟着名称走（textEdited 只响应用户输入，不会被 setText 触发）
    connect(ui->dataDirEdit, &QLineEdit::textEdited, this, [this]() {
        m_dataDirEdited = true;
        updateDataDirState();
        updatePreview();
    });

    connect(ui->browseDataDirButton, &QPushButton::clicked, this, &ChromeProfileForm::browseDataDir);
    connect(ui->globalDataDirButton, &QPushButton::clicked, this, &ChromeProfileForm::useGlobalDataDir);

    ui->browseDataDirButton->setToolTip(
        QStringLiteral("给这个多开单独指定数据目录（目录已存在时会沿用里面的登录数据）"));
    ui->globalDataDirButton->setToolTip(
        QStringLiteral("把数据目录改回全局位置下的同名子目录；点「确定」后会问你是否迁移已有数据"));
    ui->previewEdit->setPlaceholderText(QStringLiteral("填写名称后显示实际执行的命令行"));

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

    // 只有「指定代理服务器」才回填地址；其它模式输入框保持空（与置灰状态一致）
    if (profile.proxyMode == ChromeProfile::ProxyCustom)
        ui->proxyServerEdit->setText(profile.proxyServer);
    else
        ui->proxyServerEdit->clear();

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
    ui->noImagesCheckBox->setChecked(profile.noImages);

    // 三个开窗方式互斥：按「最大化 → 全屏 → 演示」的顺序勾选，
    // 后勾的会取消前面的，最终与 optionSummary/buildArguments 的优先级一致。
    ui->maximizedCheckBox->setChecked(profile.startMaximized);
    ui->fullscreenCheckBox->setChecked(profile.startFullscreen);
    ui->kioskCheckBox->setChecked(profile.kiosk);

    // 数据目录：新建时先空着（填了名称再生成）；已有配置用它记住的绝对路径。
    // 和全局目录下的同名子目录一致时，之后改名称会跟着走；用户指定过就保持不动。
    const QString dataDir = profile.name.trimmed().isEmpty()
                                ? QString()
                                : ChromeMultiOpen::profileDataDir(profile);
    m_dataDirEdited = !dataDir.isEmpty()
                      && !ChromeMultiOpen::samePath(dataDir, ChromeMultiOpen::dataDirFor(profile.name));
    ui->dataDirEdit->setText(dataDir.isEmpty() ? QString() : QDir::toNativeSeparators(dataDir));
    ui->dataDirEdit->setCursorPosition(0);

    setWindowTitle(m_originalName.isEmpty()
                       ? QStringLiteral("新建 Chrome 多开")
                       : QStringLiteral("编辑 Chrome 多开 - %1").arg(m_originalName));

    updateEnabledStates();
    updateDataDirState();
    updatePreview();
}

ChromeProfile ChromeProfileForm::profile() const
{
    ChromeProfile profile;

    profile.name = ui->nameEdit->text().trimmed();
    profile.dataDir = currentDataDir();

    profile.proxyMode = ui->proxyModeCombo->currentIndex();
    // 非「指定代理服务器」时不保留地址，下次切换回来也是干净的
    profile.proxyServer = profile.proxyMode == ChromeProfile::ProxyCustom
                              ? stripped(ui->proxyServerEdit->text())
                              : QString();

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
    profile.noImages = ui->noImagesCheckBox->isChecked();

    profile.useWindowName = ui->windowNameCheckBox->isChecked();
    profile.windowName = stripped(ui->windowNameEdit->text());

    return profile;
}

QString ChromeProfileForm::currentDataDir() const
{
    // 新建时没有目录输入框：直接按名称推导成「全局位置\<名称>」，创建时一并记进配置
    if (m_mode == CreateMode) {
        const QString name = ui->nameEdit->text().trimmed();
        QString reason;
        if (!ChromeMultiOpen::isValidName(name, &reason))
            return QString();
        return QDir::cleanPath(ChromeMultiOpen::dataDirFor(name));
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(ui->dataDirEdit->text().trimmed()));
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

    // 名称是列表和托盘菜单里的显示名，重名会分不清
    for (const ChromeProfile &other : m_others) {
        if (other.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("已经有一个叫「%1」的多开了，请换一个名称。")
                                 .arg(name));
        ui->nameEdit->setFocus();
        ui->nameEdit->selectAll();
        return;
    }

    // 数据目录：必填、必须是绝对路径、不能和其它多开共用
    const QString dataDir = currentDataDir();
    if (dataDir.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("请填写数据目录。\n\n"
                                            "默认使用全局数据位置下的同名子目录，"
                                            "也可以点「浏览…」指定别的位置。"));
        return;
    }
    if (!QDir::isAbsolutePath(dataDir)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("数据目录需要是绝对路径（例如 D:\\Chrome多开\\工作号）。"));
        return;
    }

    const QFileInfo dataDirInfo(dataDir);
    if (dataDirInfo.exists() && !dataDirInfo.isDir()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("数据目录指向了一个文件，请选择文件夹：\n%1")
                                 .arg(QDir::toNativeSeparators(dataDir)));
        return;
    }

    for (const ChromeProfile &other : m_others) {
        if (!ChromeMultiOpen::samePath(ChromeMultiOpen::profileDataDir(other), dataDir))
            continue;

        // 新建时目录是自动生成的、不能改，所以提示里要告诉用户换名称或先挪开别人的目录
        const QString text =
            m_mode == CreateMode
                ? QStringLiteral("按这个名称生成的数据目录已经被多开「%1」使用了：\n%2\n\n"
                                 "请换一个名称；或者先编辑「%1」把它的数据目录挪开。")
                      .arg(other.name, QDir::toNativeSeparators(dataDir))
                : QStringLiteral("这个数据目录已经被多开「%1」使用了。\n\n"
                                 "两个多开共用同一个目录会抢同一份登录状态，请换一个目录。")
                      .arg(other.name);
        QMessageBox::warning(this, windowTitle(), text);
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

    // 名称前后的空格不进入显示名和默认目录名
    if (ui->nameEdit->text() != name)
        ui->nameEdit->setText(name);

    QDialog::accept();
}

void ChromeProfileForm::updateEnabledStates()
{
    // 只有「指定代理服务器」才需要填地址，其余两种模式输入框直接置灰
    ui->proxyServerEdit->setEnabled(ui->proxyModeCombo->currentIndex() == ChromeProfile::ProxyCustom);

    ui->userAgentEdit->setEnabled(ui->userAgentCheckBox->isChecked());
    ui->appUrlEdit->setEnabled(ui->appModeCheckBox->isChecked());
    ui->windowWidthSpin->setEnabled(ui->windowSizeCheckBox->isChecked());
    ui->windowHeightSpin->setEnabled(ui->windowSizeCheckBox->isChecked());
    ui->windowXSpin->setEnabled(ui->windowPositionCheckBox->isChecked());
    ui->windowYSpin->setEnabled(ui->windowPositionCheckBox->isChecked());
    ui->windowNameEdit->setEnabled(ui->windowNameCheckBox->isChecked());
}

void ChromeProfileForm::updateDataDirState()
{
    const QString name = ui->nameEdit->text().trimmed();

    QString reason;
    const bool nameValid = ChromeMultiOpen::isValidName(name, &reason);
    const QString globalDir = nameValid ? ChromeMultiOpen::dataDirFor(name) : QString();

    // 没手动指定过目录时，目录跟着名称走：<全局数据位置>\<名称>
    if (!m_dataDirEdited) {
        ui->dataDirEdit->setText(globalDir.isEmpty() ? QString()
                                                     : QDir::toNativeSeparators(globalDir));
        ui->dataDirEdit->setCursorPosition(0);
    }

    const QString dir = currentDataDir();
    const bool differsFromGlobal = !dir.isEmpty() && !globalDir.isEmpty()
                                   && !ChromeMultiOpen::samePath(dir, globalDir);
    ui->globalDataDirButton->setEnabled(differsFromGlobal);

    if (dir.isEmpty()) {
        ui->dataDirHintLabel->setText(
            QStringLiteral("填写名称后自动使用全局数据位置下的同名子目录；也可以点「浏览…」单独指定。"));
        return;
    }
    if (!QDir::isAbsolutePath(dir)) {
        ui->dataDirHintLabel->setText(
            QStringLiteral("请填写绝对路径，例如 D:\\Chrome多开\\工作号。"));
        return;
    }

    QStringList lines;
    lines << (QFileInfo(dir).isDir() ? QStringLiteral("目录已存在，将沿用其中的登录状态。")
                                     : QStringLiteral("目录尚未创建，首次启动时自动创建。"));
    if (nameValid && differsFromGlobal)
        lines << QStringLiteral("与全局数据位置不同；确定时会问你是否把旧目录的数据迁移过来。");

    ui->dataDirHintLabel->setText(lines.join(QLatin1Char('\n')));
}

void ChromeProfileForm::browseDataDir()
{
    const QString start = currentDataDir().isEmpty() ? ChromeMultiOpen::dataRoot()
                                                     : currentDataDir();
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择这个多开的数据目录"), QDir::toNativeSeparators(start));
    if (directory.isEmpty())
        return;

    ui->dataDirEdit->setText(
        QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(directory))));
    ui->dataDirEdit->setCursorPosition(0);
    m_dataDirEdited = true;
    updateDataDirState();
    updatePreview();
}

void ChromeProfileForm::useGlobalDataDir()
{
    // 一键改回全局目录（<全局数据位置>\<名称>）；真正的搬迁在点「确定」后询问
    m_dataDirEdited = false;
    updateDataDirState();
    updatePreview();
}

void ChromeProfileForm::updatePreview()
{
    const ChromeProfile current = profile();

    // 还没填名称时没有确定的数据目录，先显示占位提示，别展示一条错误的命令行
    if (current.dataDir.isEmpty()) {
        ui->previewEdit->clear();
        return;
    }

    ui->previewEdit->setText(ChromeMultiOpen::commandLine(current));
    ui->previewEdit->setCursorPosition(0);
}
