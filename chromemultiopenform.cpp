#include "chromemultiopenform.h"
#include "ui_chromemultiopenform.h"

#include "chromeprofileform.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QDesktopServices>
#include <QDropEvent>
#include <QTimer>

namespace {

enum Column
{
    ColumnName = 0,
    ColumnProxy,
    ColumnOptions,
    ColumnDataDir,
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

    // 列表只读、整行选中；前三列固定宽度，数据目录列吃掉剩余宽度（有自定义目录后必须能看见）
    ui->profileTable->setColumnCount(ColumnCount);
    ui->profileTable->setHorizontalHeaderLabels({QStringLiteral("名称"),
                                                 QStringLiteral("代理"),
                                                 QStringLiteral("启动选项"),
                                                 QStringLiteral("数据目录")});
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
    ui->profileTable->horizontalHeader()->setSectionResizeMode(ColumnOptions, QHeaderView::Interactive);
    ui->profileTable->horizontalHeader()->setSectionResizeMode(ColumnDataDir, QHeaderView::Stretch);
    // 拖动行调整顺序：InternalMove 只负责让拖动/落点提示成立，
    // 真正的重排交给 eventFilter（QTableWidget 自带的内部移动会在落点复制一份、源行留空）
    ui->profileTable->setDragEnabled(true);
    ui->profileTable->setAcceptDrops(true);
    ui->profileTable->viewport()->setAcceptDrops(true);
    ui->profileTable->setDropIndicatorShown(true);
    ui->profileTable->setDragDropMode(QAbstractItemView::InternalMove);
    ui->profileTable->setDefaultDropAction(Qt::MoveAction);
    ui->profileTable->setDragDropOverwriteMode(false);
    ui->profileTable->viewport()->installEventFilter(this);

    ui->profileTable->setColumnWidth(ColumnName, 180);
    ui->profileTable->setColumnWidth(ColumnProxy, 170);
    ui->profileTable->setColumnWidth(ColumnOptions, 200);

    // 数据位置那一行：路径输入框吃掉多余宽度，四个按钮保持原宽
    ui->dataLayout->setColumnStretch(1, 1);

    // 窗口里的输入框只有一个只读命令框，回车不该误触发任何按钮
    ui->newButton->setAutoDefault(false);
    ui->editButton->setAutoDefault(false);
    ui->duplicateButton->setAutoDefault(false);
    ui->deleteButton->setAutoDefault(false);
    ui->upButton->setAutoDefault(false);
    ui->downButton->setAutoDefault(false);
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
    connect(ui->upButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedRow();
        moveProfile(row, row - 1);
    });
    connect(ui->downButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedRow();
        moveProfile(row, row + 1);
    });
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

            const QString dataDir = QDir::toNativeSeparators(ChromeMultiOpen::profileDataDir(profile));
            auto *dirItem = new QTableWidgetItem(dataDir);
            dirItem->setToolTip(dataDir);   // 列宽不够时显示省略号，悬停看完整路径
            ui->profileTable->setItem(row, ColumnDataDir, dirItem);
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
    const int row = selectedRow();
    const bool hasSelection = row >= 0;

    ui->editButton->setEnabled(hasSelection);
    ui->duplicateButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->launchButton->setEnabled(hasSelection);
    ui->upButton->setEnabled(hasSelection && row > 0);
    ui->downButton->setEnabled(hasSelection && row + 1 < m_profiles.size());
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
                   .arg(profile.name, QDir::toNativeSeparators(profile.dataDir)));
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

    // 数据目录可能因为改名被自动带过去，也可能是用户手动改的 / 点了「用全局目录」。
    // 不管哪种，只要换了位置就问一句要不要把数据一起搬过去。
    const QString oldDir = ChromeMultiOpen::profileDataDir(original);
    const QString newDir = ChromeMultiOpen::profileDataDir(updated);
    const bool dirChanged = !ChromeMultiOpen::samePath(oldDir, newDir);
    bool migrated = false;

    if (dirChanged) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            QStringLiteral("这个多开的数据目录变了：\n\n旧：%1\n新：%2\n\n"
                           "是否把旧目录里的数据迁移到新目录？\n\n"
                           "是：搬过去，成功后删除旧目录（迁移前请先关掉这个多开的 Chrome 窗口）\n"
                           "否：只改配置，旧目录保持原样")
                .arg(QDir::toNativeSeparators(oldDir), QDir::toNativeSeparators(newDir)),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Cancel)
            return;

        if (answer == QMessageBox::Yes) {
            QString error;
            if (!ChromeMultiOpen::migrateDataDirectory(oldDir, newDir, &error)) {
                // 搬不动（目标已有数据 / 文件被占用）时给个退路：只改配置不搬数据
                const auto retry = QMessageBox::question(
                    this, windowTitle(),
                    QStringLiteral("迁移没有完成：\n\n%1\n\n"
                                   "是否仍然使用新的数据目录（不迁移数据）？")
                        .arg(error),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (retry != QMessageBox::Yes)
                    return;
            } else {
                migrated = true;
            }
        }
    }

    QList<ChromeProfile> profiles = m_profiles;
    profiles[row] = updated;
    if (!storeProfiles(profiles))
        return;

    reload(updated.name);

    if (migrated) {
        setMessage(QStringLiteral("已保存「%1」，数据已迁移到：%2")
                       .arg(updated.name, QDir::toNativeSeparators(newDir)));
    } else if (dirChanged) {
        setMessage(QStringLiteral("已保存「%1」；数据没有迁移，仍留在：%2")
                       .arg(updated.name, QDir::toNativeSeparators(oldDir)));
    } else {
        setMessage(QStringLiteral("已保存多开「%1」。").arg(updated.name));
    }
}

void ChromeMultiOpenForm::duplicateSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    ChromeProfile copy = m_profiles.at(row);
    copy.name = uniqueCopyName(copy.name);
    // 复制出来的必须是新目录：共用同一个目录会抢同一份登录状态
    copy.dataDir = ChromeMultiOpen::dataDirFor(copy.name);

    QList<ChromeProfile> profiles = m_profiles;
    profiles.insert(row + 1, copy);
    if (!storeProfiles(profiles))
        return;

    reload(copy.name);
    setMessage(QStringLiteral("已复制为「%1」：参数照搬，数据目录是新的：%2%3")
                   .arg(copy.name, QDir::toNativeSeparators(copy.dataDir),
                        QFileInfo(copy.dataDir).isDir()
                            ? QStringLiteral("（该目录已存在，会沿用其中的数据）")
                            : QString()));
}

void ChromeMultiOpenForm::deleteSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    const ChromeProfile profile = m_profiles.at(row);
    const QString dataDir = QDir::toNativeSeparators(ChromeMultiOpen::profileDataDir(profile));

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

void ChromeMultiOpenForm::moveProfile(int from, int to)
{
    if (from < 0 || from >= m_profiles.size())
        return;
    if (to < 0)
        to = 0;
    if (to >= m_profiles.size())
        to = int(m_profiles.size()) - 1;
    if (from == to)
        return;

    const QString moving = m_profiles.at(from).name;

    QList<ChromeProfile> profiles = m_profiles;
    profiles.move(from, to);
    if (!storeProfiles(profiles))
        return;

    reload(moving);
    setMessage(QStringLiteral("已把「%1」调到第 %2 位，托盘菜单顺序同步更新。")
                   .arg(moving)
                   .arg(to + 1));
}

bool ChromeMultiOpenForm::eventFilter(QObject *watched, QEvent *event)
{
    // 表格拖放：把 QDropEvent 拦下来自己算「从第几行挪到第几行」。
    // 直接用 QTableWidget 的内部移动，落点会插入副本、源行留空，还要事后清理。
    if (watched == ui->profileTable->viewport() && event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);

        const int from = selectedRow();
        const QPoint pos = dropEvent->position().toPoint();
        int to = ui->profileTable->rowAt(pos.y());

        // 落在最后一行下面（表格空白处）就当成放到末尾
        if (to < 0)
            to = pos.y() > 0 ? int(m_profiles.size()) - 1 : 0;

        dropEvent->accept();

        if (from < 0 || to < 0 || from == to) {
            // 没有实际移动：忽略这次落点（拖回原位时也能看到「不可放置」的提示）
            dropEvent->setDropAction(Qt::IgnoreAction);
            return true;
        }

        // 接受成 MoveAction，拖动光标才是正常的「移动」样式；
        // 视图在拖放收尾时会按当前选中行删一次模型行（InternalMove 的固定动作），
        // 先清掉选中让它什么也删不到，真正的重排在下一轮事件循环里做。
        dropEvent->setDropAction(Qt::MoveAction);
        ui->profileTable->clearSelection();
        QTimer::singleShot(0, this, [this, from, to]() { moveProfile(from, to); });
        return true;
    }

    return QDialog::eventFilter(watched, event);
}

void ChromeMultiOpenForm::changeDataRoot()
{
    const QString current = QDir::toNativeSeparators(ChromeMultiOpen::dataRoot());
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 Chrome 多开数据位置"), current);
    if (directory.isEmpty())
        return;

    applyDataRoot(directory);
}

void ChromeMultiOpenForm::resetDataRoot()
{
    applyDataRoot(ChromeMultiOpen::defaultDataRoot());

    // 已经回到默认位置时把 ini 里的自定义值清掉（等价于默认位置，exe 换目录时还能跟着走）
    if (ChromeMultiOpen::samePath(ChromeMultiOpen::dataRoot(), ChromeMultiOpen::defaultDataRoot()))
        ChromeMultiOpen::setDataRoot(QString());
}

void ChromeMultiOpenForm::applyDataRoot(const QString &newRoot)
{
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(newRoot.trimmed()));
    if (root.isEmpty())
        return;

    if (ChromeMultiOpen::samePath(root, ChromeMultiOpen::dataRoot())) {
        setMessage(QStringLiteral("数据位置没有变化：%1")
                       .arg(QDir::toNativeSeparators(ChromeMultiOpen::dataRoot())));
        return;
    }

    // 只有确实有多个开的目录会变时才值得问；全部已经在新位置下就只更新设置
    bool anyChange = false;
    for (const ChromeProfile &profile : m_profiles) {
        if (!ChromeMultiOpen::samePath(ChromeMultiOpen::profileDataDir(profile),
                                       QDir(root).filePath(profile.name))) {
            anyChange = true;
            break;
        }
    }

    // 已有多个开时问一句：是搬数据，还是只改「新建多开的默认位置」
    bool migrate = false;
    if (anyChange) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            QStringLiteral("已有 %1 个多开。\n\n是否把它们的数据一起迁到新位置？\n\n"
                           "是：数据目录改为「新位置\\名称」，并把已有数据搬过去"
                           "（迁移前请先关掉这些多开的 Chrome 窗口）\n"
                           "否：只改全局默认位置，已有的多开继续用各自原来的目录")
                .arg(int(m_profiles.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        migrate = (answer == QMessageBox::Yes);
    }

    QList<ChromeProfile> profiles = m_profiles;
    QStringList failures;
    int migratedCount = 0;

    if (migrate) {
        for (ChromeProfile &profile : profiles) {
            const QString oldDir = ChromeMultiOpen::profileDataDir(profile);
            const QString newDir = QDir(root).filePath(profile.name);
            if (ChromeMultiOpen::samePath(oldDir, newDir)) {
                profile.dataDir = newDir;
                continue;
            }

            QString error;
            if (ChromeMultiOpen::migrateDataDirectory(oldDir, newDir, &error)) {
                profile.dataDir = newDir;
                ++migratedCount;
            } else {
                failures << QStringLiteral("「%1」：%2").arg(profile.name, error);
            }
        }
    }

    ChromeMultiOpen::setDataRoot(root);

    // 不管迁不迁移，都把每个多开的绝对目录写回配置：
    // 以后再改全局位置，它们不会再被悄悄指到别处
    if (!profiles.isEmpty() && !storeProfiles(profiles))
        return;

    reload();

    if (!failures.isEmpty()) {
        setMessage(QStringLiteral("数据位置已改为 %1，但有 %2 个多开没迁移成功，仍用各自原来的目录。")
                       .arg(QDir::toNativeSeparators(root))
                       .arg(failures.size()),
                   true);
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("以下多开没有迁移，仍使用各自原来的目录：\n\n%1")
                                 .arg(failures.join(QLatin1Char('\n'))));
        return;
    }

    if (migrate) {
        setMessage(QStringLiteral("数据位置已改为：%1，共迁移 %2 个多开的数据。")
                       .arg(QDir::toNativeSeparators(root))
                       .arg(migratedCount));
    } else if (anyChange) {
        setMessage(QStringLiteral("数据位置已改为：%1；已有的多开继续用各自原来的目录，"
                                  "新建的多开才会放到新位置。")
                       .arg(QDir::toNativeSeparators(root)));
    } else {
        setMessage(QStringLiteral("数据位置已改为：%1").arg(QDir::toNativeSeparators(root)));
    }
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
