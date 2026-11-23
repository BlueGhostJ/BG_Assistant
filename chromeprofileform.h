#ifndef CHROMEPROFILEFORM_H
#define CHROMEPROFILEFORM_H

#include <QDialog>
#include <QList>

#include "chromemultiopen.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ChromeProfileForm;
}
QT_END_NAMESPACE

// 新建 / 编辑一个 Chrome 多开：
// 名称（必填）+ 代理 + User-Agent + 独立窗口 + 窗口大小/位置 + 最大化/全屏/演示/缩放 + 窗口名称。
// 每项可选参数都配一个勾选框，不勾选就不往命令行里传对应参数。
class ChromeProfileForm : public QDialog
{
    Q_OBJECT

public:
    // others：已有的其它多开，用来做重名检查（不包含正在编辑的这一项）
    explicit ChromeProfileForm(const QList<ChromeProfile> &others, QWidget *parent = nullptr);
    ~ChromeProfileForm() override;

    void setProfile(const ChromeProfile &profile);
    ChromeProfile profile() const;

private:
    Ui::ChromeProfileForm *ui;

    QList<ChromeProfile> m_others;
    QString m_originalName;   // 编辑前的名称，只用来决定窗口标题

    void submit();            // 校验通过后调用 QDialog::accept()
    void updateEnabledStates();
    void updateNameHint();
    void updatePreview();
};

#endif // CHROMEPROFILEFORM_H
