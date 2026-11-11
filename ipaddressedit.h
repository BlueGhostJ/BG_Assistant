#ifndef IPADDRESSEDIT_H
#define IPADDRESSEDIT_H

#include <QWidget>

class QLineEdit;
class QMouseEvent;
class QPaintEvent;

// 仿 Windows IP 地址控件（SysIPAddress32）的输入框：四段数字、点号分隔、段内自动跳转。
// 原易语言版用的就是系统 IP 地址控件，Qt 没有对应控件，这里用 4 个无边框 QLineEdit 拼出来；
// 点号在 paintEvent 里按各段实际位置绘制，窗口缩放或换字体都不会错位。
class IpAddressEdit : public QWidget
{
    Q_OBJECT

public:
    explicit IpAddressEdit(QWidget *parent = nullptr);

    // 四段全部填写时返回 "a.b.c.d"，否则返回空串。
    QString address() const;
    bool isComplete() const;
    bool isEmpty() const;

    // 解析 "a.b.c.d"；非法段会被清空。不发 addressChanged（调用方自己知道改了值）。
    void setAddress(const QString &ip);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void addressChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLineEdit *m_part[4] = {nullptr, nullptr, nullptr, nullptr};
    bool m_updating = false;   // 程序化改值期间屏蔽 textEdited 引发的跳段/递归

    void distribute(const QString &text, int from);
    void focusPart(int index);
};

#endif // IPADDRESSEDIT_H
