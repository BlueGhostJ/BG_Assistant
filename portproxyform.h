#ifndef PORTPROXYFORM_H
#define PORTPROXYFORM_H

#include <QDialog>
#include <QVector>

#include "portproxy.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class PortProxyForm;
}
QT_END_NAMESPACE

// 端口转发管理（netsh interface portproxy 的 v4tov4 表）：
// 上方列出当前已有的转发，下方填写参数做添加 / 修改 / 删除。
// 修改表项没有可用的 netsh 命令，统一按「删旧 + 加新」执行。
// 需要管理员权限，由 main.cpp 以 PortProxyManager 命令行参数提权启动子进程后显示。
class PortProxyForm : public QDialog
{
    Q_OBJECT

public:
    explicit PortProxyForm(QWidget *parent = nullptr);
    ~PortProxyForm() override;

private slots:
    void refresh();
    void addProxy();
    void updateSelected();
    void deleteSelected();
    void handleSelectionChanged();

private:
    Ui::PortProxyForm *ui;

    QVector<PortProxyEntry> m_entries;   // 与表格行一一对应
    PortProxyEntry m_selected;           // 选中行的原始规则；listenPort == 0 表示没有选中

    bool readForm(PortProxyEntry *entry);
    void fillForm(const PortProxyEntry &entry);
    bool selectEntry(const PortProxyEntry &entry);
    bool containsEntry(const PortProxyEntry &entry) const;
    bool execute(PortProxyAction action, const PortProxyEntry &entry, QString *error);
    void setMessage(const QString &text, bool warning = false);
    void updateButtons();
    void updateServiceState();
};

#endif // PORTPROXYFORM_H
