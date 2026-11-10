#ifndef CONFIGFORM_H
#define CONFIGFORM_H

#include <QDialog>

namespace Ui {
class ConfigForm;
}

class ConfigForm : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigForm(QDialog *parent = nullptr);
    ~ConfigForm();

private slots:
    void on_autoStartCheckBox_stateChanged(int state);

private:
    Ui::ConfigForm *ui;
    void loadSettings();
};

#endif // CONFIGFORM_H
