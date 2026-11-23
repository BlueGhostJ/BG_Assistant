#ifndef CHROMEMULTIOPENFORM_H
#define CHROMEMULTIOPENFORM_H

#include <QDialog>
#include <QList>

#include "chromemultiopen.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ChromeMultiOpenForm;
}
QT_END_NAMESPACE

// Chrome 多开管理窗口：
// 上方是多开列表（新建 / 编辑 / 复制 / 删除 / 启动），中间显示选中项实际执行的命令行，
// 下方是数据位置（可改成已有的多开目录来沿用旧数据）。
// 由 main.cpp 以 ChromeMultiOpen 命令行参数另起进程显示，不需要管理员权限。
class ChromeMultiOpenForm : public QDialog
{
    Q_OBJECT

public:
    explicit ChromeMultiOpenForm(QWidget *parent = nullptr);
    ~ChromeMultiOpenForm() override;

protected:
    // 表格拖动排序：拖放本身只用来发起拖动，落点由这里自己算（见 .cpp 里的说明）
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Ui::ChromeMultiOpenForm *ui;

    QList<ChromeProfile> m_profiles;   // 与表格行一一对应
    QString m_selectedName;            // 选中项的名称：表格重建后按名称恢复，避免行号错位

    int selectedRow() const;
    void reload(const QString &selectName = QString());
    void updateButtons();
    void setMessage(const QString &text, bool warning = false);

    void newProfile();
    void editProfile(int row);
    void duplicateSelected();
    void deleteSelected();
    void launchSelected();

    void moveProfile(int from, int to);   // 调整顺序并写回配置（托盘菜单同步）

    void changeDataRoot();
    void resetDataRoot();
    void openDataRoot();
    void applyDataRoot(const QString &newRoot);   // 改全局位置：已有多个开时询问是否迁移数据

    bool storeProfiles(const QList<ChromeProfile> &profiles);
    bool nameExists(const QString &name) const;
    QString uniqueCopyName(const QString &base) const;
};

#endif // CHROMEMULTIOPENFORM_H
