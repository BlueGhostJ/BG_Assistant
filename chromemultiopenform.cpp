#include "chromemultiopenform.h"
#include "ui_chromemultiopenform.h"

#include "chromeprofileform.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
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

// 迁移三选一：迁移 / 不迁移 / 取消（关掉窗口或按 Esc 都算取消）
enum class MigrateChoice
{
    Migrate,
    Keep,
    Cancel
};

MigrateChoice askMigrate(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(parent);
    box.setWindowTitle(title);
    box.setIcon(QMessageBox::Question);
    box.setText(text);

    QPushButton *migrateButton = box.addButton(QStringLiteral("迁移"), QMessageBox::YesRole);
    QPushButton *keepButton = box.addButton(QStringLiteral("不迁移"), QMessageBox::NoRole);
    QPushButton *cancelButton = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);

    box.setDefaultButton(migrateButton);
    box.setEscapeButton(cancelButton);
    box.exec();

    if (box.clickedButton() == migrateButton)
        return MigrateChoice::Migrate;
    if (box.clickedButton() == keepButton)
        return MigrateChoice::Keep;
    return MigrateChoice::Cancel;
}

} // namespace

ChromeMultiOpenForm::ChromeMultiOpenForm(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChromeMultiOpenForm)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/icons/app.ico")));
    setAttribute(Qt::WA_QuitOnClose, false);

    // QDialog 默认只有关闭按钮：这里补上最小化 / 最大化，并去掉标题栏上的「?」帮助按钮
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                   | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

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

    // 窗口里没有需要回车的输入框，按钮统一关掉 autoDefault，避免误触
    ui->newButton->setAutoDefault(false);
    ui->editButton->setAutoDefault(false);
    ui->duplicateButton->setAutoDefault(false);
    ui->deleteButton->setAutoDefault(false);
    ui->upButton->setAutoDefault(false);
    ui->downButton->setAutoDefault(false);
    ui->launchButton->setAutoDefault(false);
    ui->changeRootButton->setAutoDefault(false);
    ui->defaultRootButton->setAutoDefault(false);
    ui->openRootButton->setAutoDefault(false);

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

    // 双击行的动作：编辑 / 启动，记在 ini 里，下次打开还是上次的选择
    ui->doubleClickCombo->setCurrentIndex(ChromeMultiOpen::doubleClickAction());
    connect(ui->doubleClickCombo, &QComboBox::currentIndexChanged, this, [](int index) {
        ChromeMultiOpen::setDoubleClickAction(index);
    });

    connect(ui->changeRootButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::changeDataRoot);
    connect(ui->defaultRootButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::resetDataRoot);
    connect(ui->openRootButton, &QPushButton::clicked, this, &ChromeMultiOpenForm::openDataRoot);

    connect(ui->profileTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = selectedRow();
        m_selectedName = (row >= 0 && row < m_profiles.size()) ? m_profiles.at(row).name : QString();
        updateButtons();
    });
    // 列表右键菜单：启动 / 编辑 / 删除
    ui->profileTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->profileTable, &QTableWidget::customContextMenuRequested, this,
            &ChromeMultiOpenForm::showTableMenu);

    // 双击行的动作跟随工具栏里的选择（默认编辑）
    connect(ui->profileTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (ui->doubleClickCombo->currentIndex() == ChromeMultiOpen::DoubleClickLaunch)
            launchProfile(row);
        else
            editProfile(row);
    });

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
    ChromeProfileForm dialog(ChromeProfileForm::CreateMode, m_profiles, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ChromeProfile profile = dialog.profile();
    QList<ChromeProfile> profiles = m_profiles;
    profiles.append(profile);
    if (!storeProfiles(profiles))
        return;

    reload(profile.name);
}

void ChromeMultiOpenForm::editProfile(int row)
{
    if (row < 0 || row >= m_profiles.size())
        return;

    const ChromeProfile original = m_profiles.at(row);

    // 重名检查排除自己
    QList<ChromeProfile> others = m_profiles;
    others.removeAt(row);

    ChromeProfileForm dialog(ChromeProfileForm::EditMode, others, this);
    dialog.setProfile(original);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ChromeProfile updated = dialog.profile();

    // 数据目录可能因为改名被自动带过去，也可能是用户手动改的 / 点了「用全局目录」。
    // 不管哪种，只要换了位置就问一句要不要把数据一起搬过去。
    const QString oldDir = ChromeMultiOpen::profileDataDir(original);
    const QString newDir = ChromeMultiOpen::profileDataDir(updated);
    const bool dirChanged = !ChromeMultiOpen::samePath(oldDir, newDir);

    if (dirChanged) {
        const MigrateChoice choice = askMigrate(
            this, windowTitle(),
            QStringLiteral("这个多开的数据目录变了：\n\n旧：%1\n新：%2\n\n"
                           "是否把旧目录里的数据迁移到新目录？")
                .arg(QDir::toNativeSeparators(oldDir), QDir::toNativeSeparators(newDir)));
        if (choice == MigrateChoice::Cancel)
            return;

        if (choice == MigrateChoice::Migrate) {
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
            }
        }
    }

    QList<ChromeProfile> profiles = m_profiles;
    profiles[row] = updated;
    if (!storeProfiles(profiles))
        return;

    reload(updated.name);
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
}

void ChromeMultiOpenForm::deleteSelected()
{
    deleteProfile(selectedRow());
}

void ChromeMultiOpenForm::deleteProfile(int row)
{
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
}

void ChromeMultiOpenForm::launchSelected()
{
    launchProfile(selectedRow());
}

void ChromeMultiOpenForm::launchProfile(int row)
{
    if (row < 0 || row >= m_profiles.size())
        return;

    QString error;
    if (!ChromeMultiOpen::launch(m_profiles.at(row), &error))
        QMessageBox::warning(this, windowTitle(), error);
}

void ChromeMultiOpenForm::showTableMenu(const QPoint &pos)
{
    // 只有点在具体某一行上才弹菜单：右键先选中它，后续动作用同一行
    const QModelIndex index = ui->profileTable->indexAt(pos);
    if (!index.isValid())
        return;

    const int row = index.row();
    if (row < 0 || row >= m_profiles.size())
        return;

    ui->profileTable->selectRow(row);

    QMenu menu(this);
    QAction *launchAction = menu.addAction(QStringLiteral("启动"));
    QAction *editAction = menu.addAction(QStringLiteral("编辑"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("删除"));

    QAction *chosen = menu.exec(ui->profileTable->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == launchAction)
        launchProfile(row);
    else if (chosen == editAction)
        editProfile(row);
    else if (chosen == deleteAction)
        deleteProfile(row);
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

    if (ChromeMultiOpen::samePath(root, ChromeMultiOpen::dataRoot()))
        return;   // 位置没变，什么都不用做

    // 新位置不能落进某个多开自己的数据目录里（含正好相等）：
    // 否则新建的多开会长在别人的数据目录里，迁移也变成「搬进自己内部」
    for (const ChromeProfile &profile : m_profiles) {
        const QString dir = ChromeMultiOpen::profileDataDir(profile);
        if (!ChromeMultiOpen::isPathInside(root, dir))
            continue;
        QMessageBox::warning(
            this, windowTitle(),
            QStringLiteral("新位置不能放在多开「%1」的数据目录里面：\n\n%2\n\n请换一个位置。")
                .arg(profile.name, QDir::toNativeSeparators(dir)));
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

    // 已有多个开时问一句：搬数据，还是只改「新建多开的默认位置」；取消则什么都不动
    bool migrate = false;
    if (anyChange) {
        const MigrateChoice choice = askMigrate(
            this, windowTitle(),
            QStringLiteral("已有 %1 个多开。\n\n是否把它们的数据一起迁到新位置？")
                .arg(int(m_profiles.size())));

        if (choice == MigrateChoice::Cancel)
            return;   // 取消：全局位置保持不变
        migrate = (choice == MigrateChoice::Migrate);
    }

    QList<ChromeProfile> profiles = m_profiles;
    QStringList failures;

    if (migrate) {
        for (ChromeProfile &profile : profiles) {
            const QString oldDir = ChromeMultiOpen::profileDataDir(profile);
            const QString newDir = QDir(root).filePath(profile.name);
            if (ChromeMultiOpen::samePath(oldDir, newDir)) {
                profile.dataDir = newDir;
                continue;
            }

            QString error;
            if (ChromeMultiOpen::migrateDataDirectory(oldDir, newDir, &error))
                profile.dataDir = newDir;
            else
                failures << QStringLiteral("「%1」：%2").arg(profile.name, error);
        }
    }

    ChromeMultiOpen::setDataRoot(root);

    // 不管迁不迁移，都把每个多开的绝对目录写回配置：
    // 以后再改全局位置，它们不会再被悄悄指到别处
    if (!profiles.isEmpty() && !storeProfiles(profiles))
        return;

    reload();

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("以下多开没有迁移，仍使用各自原来的目录：\n\n%1")
                                 .arg(failures.join(QLatin1Char('\n'))));
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
