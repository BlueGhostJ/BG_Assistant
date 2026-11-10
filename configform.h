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
    explicit ConfigForm(QWidget *parent = nullptr);
    ~ConfigForm() override;

private slots:
    void applyAutoStart(bool enabled);
    void applyKeepScreenOn(bool enabled);
    void applyPassword();

private:
    Ui::ConfigForm *ui;
    void loadSettings();
    void updatePasswordVisibility();
};

#endif // CONFIGFORM_H
