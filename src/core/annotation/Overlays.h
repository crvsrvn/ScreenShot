#pragma once

#include "core/annotation/Annotation.h"

#include <QFont>

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

// 水印（ANN-39）：文字 + 时间变量；平铺或 9 个方位；透明度、大小、间距、字体可调。覆盖整幅图像，不可拖动缩放
class WatermarkAnnotation : public Annotation {
public:
    enum class Placement { Tiled, TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
    QString type() const override { return QStringLiteral("watermark"); }
    QRectF bounds() const override { return QRectF(0, 0, m_canvas.width(), m_canvas.height()); }
    void setBounds(const QRectF&) override {}
    void translate(QPointF) override {}
    bool rotatable() const override { return false; }
    bool hitTest(QPointF, double) const override { return false; } // 不可选中（由工具栏删除）
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<WatermarkAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setCanvasSize(QSize size) { m_canvas = size; }
    void setText(const QString& t) { m_text = t; }     // 支持 {date} {time} {datetime}
    QString text() const { return m_text; }
    QString expandedText() const;
    void setPlacement(Placement p) { m_placement = p; }
    Placement placement() const { return m_placement; }
    void setOpacity(int alpha) { m_alpha = std::clamp(alpha, 0, 255); }
    int opacity() const { return m_alpha; }
    void setFont(const QFont& f) { m_font = f; }
    const QFont& font() const { return m_font; }
    void setSpacing(int px) { m_spacing = std::max(0, px); }
    int spacing() const { return m_spacing; }
    void setAngle(double degrees) { m_angle = degrees; }
    static Placement placementFromName(const QString& name);
    static QString placementName(Placement p);

private:
    QSize m_canvas;
    QString m_text = QStringLiteral("ScreenShot {date}");
    Placement m_placement = Placement::BottomRight;
    int m_alpha = 110;
    QFont m_font{QStringLiteral("Microsoft YaHei"), 18};
    int m_spacing = 120;
    double m_angle = -30; // 平铺时的倾斜角
};

} // namespace ss
