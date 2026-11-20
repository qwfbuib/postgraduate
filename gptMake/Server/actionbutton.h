#ifndef ACTIONBUTTON_H
#define ACTIONBUTTON_H

#include <QPushButton>

class ActionButton final : public QPushButton
{
public:
    enum class Glyph { Start, Stop, Add, Folder, Remove, Send, Video, Image, Notice, Cycle, Clear };
    enum class Tone { Quiet, Primary, Stop, Shortcut };

    ActionButton(const QString &text, Glyph glyph, QWidget *parent, Tone tone = Tone::Quiet);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Glyph m_glyph;
    Tone m_tone;
};

#endif
