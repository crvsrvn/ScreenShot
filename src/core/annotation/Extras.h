#pragma once

#include "core/annotation/Annotation.h"

#include <QFont>
#include <optional>

namespace ss {

// 序号（ANN-35）：圆形徽标里的编号；可带指示箭头（tail）和说明文字；编号样式数字 / 字母 / 罗马数字
class StepAnnotation : public Annotation {
public:
    enum class NumberStyle { Numeric, Alpha, Roman };
    QString type() const override { return QStringLiteral("step"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override; // 只移动，不缩放
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<StepAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setCenter(QPointF c) { m_center = c; }
    QPointF center() const { return m_center; }
    void setNumber(int n) { m_number = std::max(1, n); }
    int number() const { return m_number; }
    void setNumberStyle(NumberStyle s) { m_numberStyle = s; }
    NumberStyle numberStyle() const { return m_numberStyle; }
    void setTail(std::optional<QPointF> tail) { m_tail = tail; }
    std::optional<QPointF> tail() const { return m_tail; }
    void setText(const QString& t) { m_text = t; }
    const QString& text() const { return m_text; }
    void setFont(const QFont& f) { m_font = f; }
    const QFont& font() const { return m_font; }
    double radius() const;      // 徽标半径（随笔宽）
    QRectF badgeRect() const;
    QString label() const;      // 按样式格式化的编号
    static QString formatNumber(int n, NumberStyle style);

private:
    QRectF textRect() const;
    QPointF m_center;
    int m_number = 1;
    NumberStyle m_numberStyle = NumberStyle::Numeric;
    std::optional<QPointF> m_tail;
    QString m_text;
    QFont m_font{QStringLiteral("Microsoft YaHei"), 14};
};

// 放大标注（ANN-37）：把源区域放大画到目标位置；可选包含下层标注、阴影；连接线样式用 penStyle
class MagnifierAnnotation : public Annotation {
public:
    enum class Shape { Rect, Ellipse };
    QString type() const override { return QStringLiteral("magnifier"); }
    QRectF bounds() const override { return m_target; }
    void setBounds(const QRectF& r) override;    // 目标区域，源不变；缩放随之变化
    void translate(QPointF d) override { m_target.translate(d); }
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<MagnifierAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setSource(const QRectF& r) { m_source = r.normalized(); }
    QRectF source() const { return m_source; }
    void setTarget(const QRectF& r) { m_target = r.normalized(); }
    QRectF target() const { return m_target; }
    // 按倍数放置目标：紧挨源区域右侧（放不下则左侧 / 下方），限制在 canvas 内
    void placeTarget(double zoom, const QRectF& canvas);
    double zoom() const { return m_source.width() > 0 ? m_target.width() / m_source.width() : 1.0; }
    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setIncludeAnnotations(bool on) { m_includeAnnotations = on; }
    bool includeAnnotations() const { return m_includeAnnotations; }
    void setShadow(bool on) { m_shadow = on; }
    bool shadow() const { return m_shadow; }
    QPainterPath targetPath() const;

private:
    QRectF m_source;
    QRectF m_target;
    Shape m_shape = Shape::Rect;
    bool m_includeAnnotations = false;
    bool m_shadow = true;
};

} // namespace ss
