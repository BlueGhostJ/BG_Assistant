#include "chromemultiopenform.h"
#include "ui_chromemultiopenform.h"

#include "chromeprofileform.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QIcon>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QDesktopServices>

namespace {

enum Column
{
    ColumnName = 0,
    ColumnProxy,
    ColumnOptions,
    ColumnCount
};

// 提示文字里给「失败」用的红色：与端口转发窗口保持一致
const char *const kWarningStyle = "color: #d9534f;";

} // namespace

ChromeMultiOpenForm::ChromeMultiOpenForm(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChromeMultiOpenForm)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));
    setAttribute(Qt::WA_QuitOnClose, false);

    // 列表只读、整行选中；名称和代理按内容给宽，启动选项列吃掉剩余宽度
    ui->profileTable->setColumnCount(ColumnCount);    ui->profileTable->setHorizontalHeaderLabels({QStringLiteral("名称"),
                                                 QStringLiteral("代理"),
                                                 QStringLiteral("启动选项")});
    ui->profileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->profileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->profileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->profileTable->setAlternatingRowColors(true);
    ui->profileTable->setWordWrap(false);
    ui->profileTable->setTextElideMode(Qt::ElideRight);
    ui->profileTable->verticalHeader()->setVisible(false);
    ui->profileTable->verticalHeader()->setDefaultSectionSize(24);
    ui->profileTable->horizontalHeader()->setSectionResizeMode(ColumnName, QHeaderView::Interactive);
    ui->profileTable->horizontalHeader()->setSectionResizeMode(ColumnProxy, QHeaderView::Interactive);
    ui->profileTable->horizontalHeader()->setSectionResizeMode(ColumnOptions, QHeaderView::Stretch);
    ui->profileTable->setColumnWidth(ColumnName, 220);
    ui->profileTable->setColumnWidth(ColumnProxy, 220);

    // 数据位置那一行：路径输入框吃掉多余宽度，四个按钮保持原宽
    ui->dataLayout->setColumnStretch(1, 1);

    // 窗口里的输入框只有一个只读命令框，回车不该误触发任何按钮
    ui->newButton->setAutoDefault(false);
    ui->editButton->setAutoDefault(false);
    ui->duplicateButton->setAutoDefault(false);
    ui->deleteButton->setAutoDefault(false);
    ui->launchButton->setAutoDefault(false);
    ui->copyCommandButton->setAutoDefault(false);
    ui->changeRootButton->setAutoDefault(false);
    ui->defaultRootButton->setAutoDefault(false);
    ui->openRootButton->setAutoDefault(false);
    ui->closeButton->setAutoDefault(false);

    ui->commandEdit->setPlaceholderText(
        QStringLiteral("在列表中选中一个多开，这里显示它实际执行的命令行。"));

    connect(ui->newButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::newProfile);
    connect(ui->editButton, &QPushButton::clicked, this,
            [this]() { editProfile(selectedRow()); });
    connect(ui->duplicateButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::duplicateSelected);
    connect(ui->deleteButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::deleteSelected);
    connect(ui->launchButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::launchSelected);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(ui->changeRootButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::changeDataRoot);
    connect(ui->defaultRootButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::resetDataRoot);
    connect(ui->openRootButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::openDataRoot);

    connect(ui->copyCommandButton, &QPushButton::clicked, this, [this]() {
        if (ui->commandEdit->text().isEmpty())
            return;
        QApplication::clipboard()->setText(ui->commandEdit->text());
        setMessage(QStringLiteral("启动命令已复制到剪贴板。"));
    });

    connect(ui->profileTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = selectedRow();
        m_selectedName = (row >= 0 && row < m_profiles.size()) ? m_profiles.at(row).name : QString();
        updateButtons();
        updateCommandPreview();
    });
    // 双击某一行直接进编辑（启动有单独的按钮，避免误触拉起浏览器）
    connect(ui->profileTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { editProfile(row); });

    reload();
}

ChromeMultiOpenForm::~ChromeMultiOpenForm()
{
    delete ui;
}

int ChromeMultiOpenForm::selectedRow() const
{
    return ui->profileTable->currentRow();
}

void ChromeMultiOpenForm::reload(const QString &selectName)
{
    // 刷新后尽量回到原来选中的那一项：改名/删除后行号会错位，所以按名称找
    const QString wanted = selectName.isEmpty() ? m_selectedName : selectName;

    m_profiles = ChromeMultiOpen::loadProfiles();
    m_selectedName.clear();

    {
        const QSignalBlocker blocker(ui->profileTable);
        ui->profileTable->setUpdatesEnabled(false);
        // 先清空再重建：setRowCount(0) 会把当前行一起清掉，
        // 否则删除最后一行后 currentRow() 还会停留在越界的旧行号上
        ui->profileTable->setRowCount(0);
        ui->profileTable->setRowCount(int(m_profiles.size()));

        for (int row = 0; row < m_profiles.size(); ++row) {
            const ChromeProfile &profile = m_profiles.at(row);
            ui->profileTable->setItem(row, ColumnName, new QTableWidgetItem(profile.name));
            ui->profileTable->setItem(row, ColumnProxy,
                                      new QTableWidgetItem(ChromeMultiOpen::proxySummary(profile)));
            ui->profileTable->setItem(row, ColumnOptions,
                                      new QTableWidgetItem(ChromeMultiOpen::optionSummary(profile)));
        }

        ui->profileTable->setUpdatesEnabled(true);
    }

    ui->dataRootEdit->setText(QDir::toNativeSeparators(ChromeMultiOpen::dataRoot()));
    ui->dataRootEdit->setCursorPosition(0);

    if (!wanted.isEmpty()) {
        for (int row = 0; row < m_profiles.size(); ++row) {
            if (m_profiles.at(row).name.compare(wanted, Qt::CaseInsensitive) != 0)
                continue;
            ui->profileTable->selectRow(row);
            break;
        }
    }

    updateButtons();
    updateCommandPreview();

    setMessage(m_profiles.isEmpty()
                   ? QStringLiteral("还没有任何 Chrome 多开，点「新建」创建一个。")
                   : QStringLiteral("共 %1 个多开。").arg(int(m_profiles.size())));
}

void ChromeMultiOpenForm::updateButtons()
{
    const bool hasSelection = selectedRow() >= 0;
    ui->editButton->setEnabled(hasSelection);
    ui->duplicateButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->launchButton->setEnabled(hasSelection);
    ui->copyCommandButton->setEnabled(!ui->commandEdit->text().isEmpty());
}

void ChromeMultiOpenForm::updateCommandPreview()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size()) {
        ui->commandEdit->clear();
        ui->copyCommandButton->setEnabled(false);
        return;
    }

    ui->commandEdit->setText(ChromeMultiOpen::commandLine(m_profiles.at(row)));
    ui->commandEdit->setCursorPosition(0);
    ui->copyCommandButton->setEnabled(true);
}

void ChromeMultiOpenForm::setMessage(const QString &text, bool warning)
{
    ui->messageLabel->setText(text);
    ui->messageLabel->setStyleSheet(warning ? QString::fromLatin1(kWarningStyle) : QString());
}

bool ChromeMultiOpenForm::storeProfiles(const QList<ChromeProfile> &profiles)
{
    if (!ChromeMultiOpen::saveProfiles(profiles)) {
        QMessageBox::warning(
            this, windowTitle(),
            QStringLiteral("配置写入失败：\n%1\n\n请确认程序所在目录可写。")
                .arg(QDir::toNativeSeparators(ChromeMultiOpen::iniPath())));
        return false;
    }
    return true;
}

bool ChromeMultiOpenForm::nameExists(const QString &name) const
{
    for (const ChromeProfile &profile : m_profiles) {
        if (profile.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString ChromeMultiOpenForm::uniqueCopyName(const QString &base) const
{
    // 名称上限 64，这里留出「-副本NNN」的位置
    QString stem = base;
    if (stem.size() > 54)
        stem = stem.left(54);

    QString candidate = stem + QStringLiteral("-副本");
    int index = 2;
    while (nameExists(candidate) && index < 1000) {
        candidate = QStringLiteral("%1-副本%2").arg(stem).arg(index);
        ++index;
    }
    return candidate;
}

void ChromeMultiOpenForm::newProfile()
{
    ChromeProfileForm dialog(m_profiles, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ChromeProfile profile = dialog.profile();
    QList<ChromeProfile> profiles = m_profiles;
    profiles.append(profile);
    if (!storeProfiles(profiles))
        return;

    reload(profile.name);
    setMessage(QStringLiteral("已创建多开「%1」，数据目录：%2")
                   .arg(profile.name,
                        QDir::toNativeSeparators(ChromeMultiOpen::dataDirFor(profile.name))));
}

void ChromeMultiOpenForm::editProfile(int row)
{
    if (row < 0 || row >= m_profiles.size())
        return;

    const ChromeProfile original = m_profiles.at(row);

    // 重名检查排除自己
    QList<ChromeProfile> others = m_profiles;
    others.removeAt(row);

    ChromeProfileForm dialog(others, this);
    dialog.setProfile(original);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ChromeProfile updated = dialog.profile();

    // 名称变了 = 数据目录也变了，旧目录不会自动搬过去，先说清楚
    const QString oldDir = ChromeMultiOpen::dataDirFor(original.name);
    const QString newDir = ChromeMultiOpen::dataDirFor(updated.name);
    if (QString::compare(QDir::toNativeSeparators(oldDir), QDir::toNativeSeparators(newDir),
                         Qt::CaseInsensitive) != 0) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            QStringLiteral("名称改了，数据目录也会跟着换：\n\n"
                           "旧：%1\n新：%2\n\n"
                           "旧目录里的登录数据不会自动迁移，仍留在原处。是否继续？")
                .arg(QDir::toNativeSeparators(oldDir), QDir::toNativeSeparators(newDir)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QList<ChromeProfile> profiles = m_profiles;
    profiles[row] = updated;
    if (!storeProfiles(profiles))
        return;

    reload(updated.name);
    setMessage(QStringLiteral("已保存多开「%1」。").arg(updated.name));
}

void ChromeMultiOpenForm::duplicateSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    ChromeProfile copy = m_profiles.at(row);
    copy.name = uniqueCopyName(copy.name);

    QList<ChromeProfile> profiles = m_profiles;
    profiles.insert(row + 1, copy);
    if (!storeProfiles(profiles))
        return;

    reload(copy.name);
    setMessage(QStringLiteral("已复制为「%1」：参数照搬，数据目录是新的（首次启动是干净的 Chrome）。")
                   .arg(copy.name));
}

void ChromeMultiOpenForm::deleteSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    const ChromeProfile profile = m_profiles.at(row);
    const QString dataDir = QDir::toNativeSeparators(ChromeMultiOpen::dataDirFor(profile.name));

    const auto answer = QMessageBox::question(
        this, windowTitle(),
        QStringLiteral("确定删除多开「%1」吗？\n\n只删除这条配置，不会删除它的数据目录：\n%2\n\n"
                       "以后新建同名多开，还能接着用原来的登录数据。")
            .arg(profile.name, dataDir),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QList<ChromeProfile> profiles = m_profiles;
    profiles.removeAt(row);
    if (!storeProfiles(profiles))
        return;

    reload();
    setMessage(QStringLiteral("已删除多开「%1」，数据目录保留在：%2").arg(profile.name, dataDir));
}

void ChromeMultiOpenForm::launchSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    const ChromeProfile profile = m_profiles.at(row);
    QString error;
    if (!ChromeMultiOpen::launch(profile, &error)) {
        setMessage(QStringLiteral("启动「%1」失败。").arg(profile.name), true);
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }

    setMessage(QStringLiteral("已启动多开「%1」。").arg(profile.name));
}

void ChromeMultiOpenForm::changeDataRoot()
{
    const QString current = QDir::toNativeSeparators(ChromeMultiOpen::dataRoot());
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 Chrome 多开数据位置"), current);
    if (directory.isEmpty())
        return;

    ChromeMultiOpen::setDataRoot(directory);
    reload();
    setMessage(QStringLiteral("数据位置已改为：%1（各多开按名称到这个目录下找数据）")
                   .arg(QDir::toNativeSeparators(ChromeMultiOpen::dataRoot())));
}

void ChromeMultiOpenForm::resetDataRoot()
{
    ChromeMultiOpen::setDataRoot(QString());
    reload();
    setMessage(QStringLiteral("已恢复默认数据位置：%1")
                   .arg(QDir::toNativeSeparators(ChromeMultiOpen::dataRoot())));
}

void ChromeMultiOpenForm::openDataRoot()
{
    const QString root = ChromeMultiOpen::dataRoot();
    if (!QDir().mkpath(root)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("无法创建数据目录：\n%1")
                                 .arg(QDir::toNativeSeparators(root)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(root));
}
