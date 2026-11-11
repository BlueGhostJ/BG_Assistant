#include "ipaddressedit.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStringList>
#include <QStyle>
#include <QStyleOption>
#include <QValidator>

#include <climits>

namespace {

// 段与段之间留给点号的宽度
const int kDotGap = 12;

// 单段校验：手输限 0-255 且最多 3 位；含点号（粘贴整段 IP）时放行，由 IpAddressEdit::distribute 拆分。
// 不用 QIntValidator 就是因为它在粘贴 "192.168.1.1" 时会直接判非法，整段粘贴就用不了了。
class IpPartValidator : public QValidator
{
public:
    explicit IpPartValidator(QObject *parent = nullptr)
        : QValidator(parent)
    {
    }

    State validate(QString &input, int &pos) const override
    {
        Q_UNUSED(pos)

        if (input.contains(QLatin1Char('.')))
            return Acceptable;
        if (input.isEmpty())
            return Intermediate;

        for (QChar ch : input) {
            if (!ch.isDigit())
                return Invalid;
        }
        if (input.size() > 3)
            return Invalid;

        return input.toInt() <= 255 ? Acceptable : Invalid;
    }
};

} // namespace

IpAddressEdit::IpAddressEdit(QWidget *parent)
    : QWidget(parent)
{
    // 底色取 Base（白），边框交给样式自带的 PE_FrameLineEdit 绘制，这样在原生主题下与普通输入框一致。
    QPalette pal = palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::Base));
    setPalette(pal);
    setAutoFillBackground(true);
    // 控件本身不接收焦点：Tab/初始焦点直接落到第一段输入框，键盘输入才有去处
    setFocusPolicy(Qt::NoFocus);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(0);

    for (int i = 0; i < 4; ++i) {
        if (i > 0)
            layout->addSpacing(kDotGap);

        auto *edit = new QLineEdit(this);
        edit->setFrame(false);
        // 不设 3：粘贴完整 IP（如 192.168.1.1）时不能被截断；手输超过 3 位的非法值由 validator 挡掉
        edit->setMaxLength(15);
        edit->setValidator(new IpPartValidator(edit));
        edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        // 透明背景：点号与白底由本控件统一绘制，避免四段各自画一块底色挡住边框
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; border: none; padding: 0; }"));
        edit->installEventFilter(this);
        m_part[i] = edit;
        layout->addWidget(edit, 1);

        connect(edit, &QLineEdit::textEdited, this, [this, i](const QString &text) {
            if (m_updating)
                return;
            // 粘贴/直接输入点号：把整段 IP 拆分到各段
            if (text.contains(QLatin1Char('.'))) {
                distribute(text, i);
                return;
            }
            // "0000" 这类前导零或超长粘贴：只留前 3 位
            if (text.size() > 3) {
                m_updating = true;
                m_part[i]->setText(text.left(3));
                m_updating = false;
                m_part[i]->setCursorPosition(3);
            }
            // 满 3 位自动跳到下一段，对齐系统 IP 控件的输入手感
            if (m_part[i]->text().size() >= 3 && i < 3)
                focusPart(i + 1);
            emit addressChanged();
        });
    }

    // 外部调 setFocus() 时落到第一段（容器本身 NoFocus，没有代理就点不到里面）
    setFocusProxy(m_part[0]);
}

QString IpAddressEdit::address() const
{
    QStringList parts;
    for (int i = 0; i < 4; ++i) {
        const QString text = m_part[i]->text().trimmed();
        if (text.isEmpty())
            return QString();
        parts << QString::number(text.toInt());
    }
    return parts.join(QLatin1Char('.'));
}

bool IpAddressEdit::isComplete() const
{
    return !address().isEmpty();
}

bool IpAddressEdit::isEmpty() const
{
    for (int i = 0; i < 4; ++i) {
        if (!m_part[i]->text().trimmed().isEmpty())
            return false;
    }
    return true;
}

void IpAddressEdit::setAddress(const QString &ip)
{
    const QStringList parts = ip.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    m_updating = true;
    for (int i = 0; i < 4; ++i) {
        const QString text = i < parts.size() ? parts.at(i).trimmed() : QString();
        bool ok = false;
        const int value = text.toInt(&ok);
        m_part[i]->setText(ok && value >= 0 && value <= 255 ? QString::number(value) : QString());
    }
    m_updating = false;
}

void IpAddressEdit::clear()
{
    setAddress(QString());
}

QSize IpAddressEdit::sizeHint() const
{
    return QSize(170, qMax(24, fontMetrics().height() + 8));
}

QSize IpAddressEdit::minimumSizeHint() const
{
    return QSize(120, qMax(24, fontMetrics().height() + 8));
}

void IpAddressEdit::distribute(const QString &text, int from)
{
    const QStringList parts = text.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    // 粘贴进来的是完整 IP（四段）时从头填，否则从当前段往后填
    int index = parts.size() >= 4 ? 0 : from;

    for (const QString &part : parts) {
        if (index > 3)
            break;

        // 只保留数字，"19a2" 这种按 192 处理
        QString digits;
        for (QChar ch : part) {
            if (ch.isDigit())
                digits.append(ch);
        }

        bool ok = false;
        const int value = digits.toInt(&ok);
        if (!ok || value < 0 || value > 255)
            continue;   // 空段（用户刚敲下点号）或非法值：不占位，继续看下一段

        const bool blocked = m_updating;
        m_updating = true;
        m_part[index]->setText(QString::number(value));
        m_updating = blocked;
        ++index;
    }

    focusPart(qMin(index, 3));
    emit addressChanged();
}

void IpAddressEdit::focusPart(int index)
{
    index = qBound(0, index, 3);
    m_part[index]->setFocus(Qt::OtherFocusReason);
    m_part[index]->setCursorPosition(int(m_part[index]->text().size()));
}

bool IpAddressEdit::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    int index = -1;
    for (int i = 0; i < 4; ++i) {
        if (watched == m_part[i]) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return QWidget::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Period:
    case Qt::Key_Comma:
        focusPart(index + 1);
        return true;
    case Qt::Key_Right:
        if (index < 3 && m_part[index]->cursorPosition() >= m_part[index]->text().size()) {
            focusPart(index + 1);
            return true;
        }
        break;
    case Qt::Key_Left:
        if (index > 0 && m_part[index]->cursorPosition() == 0) {
            focusPart(index - 1);
            return true;
        }
        break;
    case Qt::Key_Backspace:
        if (index > 0 && m_part[index]->text().isEmpty()) {
            focusPart(index - 1);
            return true;
        }
        break;
    case Qt::Key_Tab:
        // 段内 Tab 视为跳到下一段；最后一段交回 Qt，正常切到下一个控件
        if (index < 3) {
            focusPart(index + 1);
            return true;
        }
        break;
    case Qt::Key_Backtab:
        if (index > 0) {
            focusPart(index - 1);
            return true;
        }
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void IpAddressEdit::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);

    // 用样式自带的输入框边框，跟随系统主题（浅色/深色、Fusion/Windows 样式都能画对）。
    // 必须用 QStyleOptionFrame：PE_FrameLineEdit 内部靠 qstyleoption_cast 取参，传基类会被忽略。
    QStyleOptionFrame option;
    option.initFrom(this);
    option.rect = rect();
    option.lineWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, this);
    option.midLineWidth = 0;
    option.state |= QStyle::State_Sunken;
    style()->drawPrimitive(QStyle::PE_FrameLineEdit, &option, &painter, this);

    // 三段点号画在相邻两段的正中间，纵向与分段文字同一条基线
    painter.setPen(palette().color(QPalette::Text));
    const QFontMetrics metrics = fontMetrics();
    const int baseline = (height() + metrics.ascent() - metrics.descent()) / 2;
    const qreal dotWidth = metrics.horizontalAdvance(QLatin1Char('.'));

    for (int i = 0; i < 3; ++i) {
        const int x = (m_part[i]->geometry().right() + m_part[i + 1]->geometry().left()) / 2;
        painter.drawText(QPointF(x - dotWidth / 2.0, baseline), QStringLiteral("."));
    }
}

void IpAddressEdit::mousePressEvent(QMouseEvent *event)
{
    // 点在点号或空白处时，把焦点交给最近的一段（系统 IP 控件也是这个手感）
    int target = 0;
    int nearest = INT_MAX;
    for (int i = 0; i < 4; ++i) {
        const QRect geometry = m_part[i]->geometry();
        int distance = 0;
        if (event->pos().x() < geometry.left())
            distance = geometry.left() - event->pos().x();
        else if (event->pos().x() > geometry.right())
            distance = event->pos().x() - geometry.right();

        if (distance < nearest) {
            nearest = distance;
            target = i;
        }
    }

    focusPart(target);
    QWidget::mousePressEvent(event);
}
