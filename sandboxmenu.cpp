#include "sandboxmenu.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

SandboxMenu::SandboxMenu(QWidget *parent)
    : QMenu(QStringLiteral("沙箱"), parent)
{
}

void SandboxMenu::refresh()
{
    m_entries = Sandboxie::loadEntries(Sandboxie::iniPath());

    // 菜单项全是动态的，整体重建最省事；重建后菜单可能被点开过，故不保留原状态
    clear();

    // 前两项固定在二级菜单最上方，与沙箱项之间用一条分隔线隔开
    QAction *commandsAction = addAction(QStringLiteral("显示启动命令"));
    connect(commandsAction, &QAction::triggered, this, &SandboxMenu::showCommands);

    QAction *refreshAction = addAction(QStringLiteral("刷新沙箱列表"));
    // 排队执行：refresh() 里的 clear() 会删掉刚被点击的这个 action，
    // 直接在它的信号里删自己容易踩到悬空指针，放到事件循环下一轮最稳
    connect(refreshAction, &QAction::triggered, this, &SandboxMenu::refresh,
            Qt::QueuedConnection);

    if (!m_entries.isEmpty()) {
        addSeparator();
        for (const SandboxEntry &entry : m_entries) {
            QAction *action = addAction(entry.boxName);
            action->setToolTip(entry.commandLine);   // 悬停可预览完整命令行，便于排查
            connect(action, &QAction::triggered, this, [this, entry]() { launchEntry(entry); });
        }
    }

    // 没有可启动的沙箱时，连一级菜单一起不显示
    menuAction()->setVisible(!m_entries.isEmpty());
}

void SandboxMenu::showCommands() const
{
    QStringList lines;
    lines << QStringLiteral("配置文件：%1").arg(Sandboxie::iniPath())
          << QStringLiteral("启动程序：%1").arg(Sandboxie::sandManPath());

    if (m_entries.isEmpty()) {
        lines << QString() << QStringLiteral("（没有解析到可启动的沙箱）");
    } else {
        for (const SandboxEntry &entry : m_entries) {
            // 还原成 ini 里的原始写法（去掉引号），方便和配置文件逐行对照
            const QString iniValue = entry.program.isEmpty()
                                         ? entry.rawPath
                                         : entry.program + QLatin1Char('|') + entry.rawPath;
            lines << QString()
                  << QStringLiteral("[%1]").arg(entry.boxName)
                  << QStringLiteral("  ini : %1").arg(iniValue)
                  << QStringLiteral("  cmd : %1").arg(entry.commandLine);
        }
    }

    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("沙箱启动命令"));

    QPlainTextEdit *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(lines.join(QLatin1Char('\n')));

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    QPushButton *copyButton =
        buttons->addButton(QStringLiteral("复制"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(copyButton, &QPushButton::clicked, view,
            [view]() { QApplication::clipboard()->setText(view->toPlainText()); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(view);
    layout->addWidget(buttons);

    dialog.resize(820, 460);
    dialog.exec();
}

void SandboxMenu::launchEntry(const SandboxEntry &entry)
{
    QString error;
    if (!Sandboxie::launch(entry, &error))
        QMessageBox::warning(nullptr, QStringLiteral("沙箱"), error);
}
