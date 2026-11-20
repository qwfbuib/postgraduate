#ifndef ADDRESSLIST_H
#define ADDRESSLIST_H

#include <QStringList>
#include <QWidget>

class AddressList final : public QWidget
{
public:
    explicit AddressList(QWidget *parent = nullptr);

    void setAddresses(const QStringList &addresses);
};

#endif
