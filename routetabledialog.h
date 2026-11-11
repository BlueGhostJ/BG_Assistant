#ifndef ROUTETABLEDIALOG_H
#define ROUTETABLEDIALOG_H

#include <QDialog>

class QLabel;
class QTableWidget;

// 系统路由表窗口：用原生表格显示 IPv4 路由，替代原版「导出临时 txt 再用 notepad++ 打开」。
class RouteTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RouteTableDialog(QWidget *parent = nullptr);

private:
    QLabel *m_summary = nullptr;
    QTableWidget *m_table = nullptr;

    void refresh();
};

#endif // ROUTETABLEDIALOG_H
