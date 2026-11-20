#include "addresslist.h"

#include <QLabel>
#include <QLayout>

namespace {

class AddressLayout final : public QLayout
{
public:
    explicit AddressLayout(QWidget *parent) : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(6);
    }

    ~AddressLayout() override
    {
        while (auto *item = takeAt(0)) {
            delete item;
        }
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < count() ? m_items.takeAt(index) : nullptr;
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }

    QSize minimumSize() const override
    {
        QSize size(0, 0);
        for (const auto *item : m_items) {
            size = size.expandedTo(item->minimumSize());
        }
        return size;
    }

    QSize sizeHint() const override { return minimumSize(); }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    int arrange(const QRect &rect, bool place) const
    {
        int x = 0;
        int y = 0;
        int lineHeight = 0;
        for (auto *item : m_items) {
            const QSize size = item->sizeHint();
            if (x > 0 && x + size.width() > rect.width()) {
                x = 0;
                y += lineHeight + spacing();
                lineHeight = 0;
            }

            if (place) {
                item->setGeometry(QRect(rect.topLeft() + QPoint(x, y), size));
            }
            x += size.width() + spacing();
            lineHeight = qMax(lineHeight, size.height());
        }
        return y + lineHeight;
    }

    QList<QLayoutItem *> m_items;
};

}

AddressList::AddressList(QWidget *parent) : QWidget(parent)
{
    auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setObjectName(QStringLiteral("localAddresses"));
    setAccessibleName(QStringLiteral("本机地址"));
    new AddressLayout(this);
}

void AddressList::setAddresses(const QStringList &addresses)
{
    auto *addressLayout = layout();
    while (auto *item = addressLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    for (const QString &address : addresses) {
        auto *label = new QLabel(address, this);
        label->setObjectName(QStringLiteral("addressTag"));
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        addressLayout->addWidget(label);
    }
    addressLayout->invalidate();
    updateGeometry();
}
