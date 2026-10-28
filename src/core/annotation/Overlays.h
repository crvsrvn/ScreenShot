#pragma once

#include "core/annotation/Annotation.h"

namespace ss {

// 聚光灯（ANN-38）：突出一块区域，其余用半透明遮罩压暗；遮罩透明度与描边可调
class SpotlightAnnotation : public Annotation {
public:
    enum class Shape { Rect, Ellipse };
    QString type() const override { return QStringLiteral("spotlight"); }
    QRectF bounds() const override { return m_rect; }
    void setBounds(const QRectF& r) override { m_rect = r.normalized(); }
    void translate(QPointF d) override { m_rect.translate(d); }
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<SpotlightAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setMaskOpacity(int alpha) { m_maskAlpha = std::clamp(alpha, 0, 255); }
    int maskOpacity() const { return m_maskAlpha; }
    QPainterPath shapePath() const;

private:
    QRectF m_rect;
    Shape m_shape = Shape::Rect;
    int m_maskAlpha = 150;
};

} // namespace ss
