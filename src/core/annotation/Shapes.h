#pragma once

#include "core/annotation/Annotation.h"

#include <QFont>
#include <QList>
#include <QPointF>
#include <optional>

namespace ss {

// 形状类：矩形、椭圆（ANN-20）
class RectAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("rect"); }
    QRectF bounds() const override { return m_rect; }
    void setBounds(const QRectF& r) override { m_rect = r.normalized(); }
    void translate(QPointF d) override { m_rect.translate(d); }
    bool usesFill() const override { return true; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<RectAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;
    // 圆角（ANN-21）：半径像素，限制在短边一半以内
    void setRadius(double r) { m_radius = std::max(0.0, r); }
    double radius() const { return std::min(m_radius, std::min(m_rect.width(), m_rect.height()) / 2.0); }
    virtual bool supportsRadius() const { return true; }

protected:
    QRectF m_rect;
    double m_radius = 0;
};

// 椭圆；通过控制点变为扇形 / 弧形（ANN-22）：startAngle / spanAngle（度，逆时针，span 360 为整圆）
class EllipseAnnotation : public RectAnnotation {
public:
    QString type() const override { return QStringLiteral("ellipse"); }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<EllipseAnnotation>(*this); }
    bool supportsRadius() const override { return false; }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;
    void setArc(double startDegrees, double spanDegrees);
    double startAngle() const { return m_start; }
    double spanAngle() const { return m_span; }
    bool isFullEllipse() const { return m_span >= 360.0; }
    // 弧的两个端点（图像坐标，未旋转）
    QPointF pointAtAngle(double degrees) const;
    QPainterPath shapePath() const;

private:
    double m_start = 0;
    double m_span = 360;
};

// 折线 / 箭头（ANN-23、ANN-25）：points 至少两个；起点与终点各自的箭头样式
class LineAnnotation : public Annotation {
public:
    enum class Head { None, Triangle, Open, Dot, Bar };
    QString type() const override { return QStringLiteral("line"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<LineAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    QList<QPointF>& points() { return m_points; }
    const QList<QPointF>& points() const { return m_points; }
    void setArrow(bool start, bool end) { m_headStart = start ? Head::Triangle : Head::None; m_headEnd = end ? Head::Triangle : Head::None; }
    void setHeads(Head start, Head end) { m_headStart = start; m_headEnd = end; }
    bool arrowEnd() const { return m_headEnd != Head::None; }
    bool arrowStart() const { return m_headStart != Head::None; }
    Head headStart() const { return m_headStart; }
    Head headEnd() const { return m_headEnd; }

    static QPainterPath arrowHead(QPointF from, QPointF to, double width);
    static QString headName(Head h);
    static Head headFromName(const QString& name);
    // 注释文字（ANN-26）：画在终点旁
    void setText(const QString& t) { m_text = t; }
    const QString& text() const { return m_text; }
    void setFont(const QFont& f) { m_font = f; }
    const QFont& font() const { return m_font; }
    QRectF textRect() const;

private:
    // 画一个端点的箭头；返回线条应缩短的长度
    double paintHead(QPainter& p, Head head, QPointF from, QPointF to) const;
    QList<QPointF> m_points;
    Head m_headStart = Head::None;
    Head m_headEnd = Head::None;
    QString m_text;
    QFont m_font{QStringLiteral("Microsoft YaHei"), 14};
};

// 铅笔（ANN-27）：自由路径；smooth=true 时用二次贝塞尔平滑
class PencilAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("pencil"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<PencilAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    QList<QPointF>& points() { return m_points; }
    const QList<QPointF>& points() const { return m_points; }
    void setSmooth(bool s) { m_smooth = s; }
    QPainterPath path() const;

protected:
    QList<QPointF> m_points;
    bool m_smooth = false;
};

// 荧光笔（ANN-28）：画笔或矩形两种形状；正片叠底或半透明两种混合
class HighlighterAnnotation : public PencilAnnotation {
public:
    enum class Shape { Brush, Rect };
    enum class Blend { Multiply, Translucent };
    QString type() const override { return QStringLiteral("highlighter"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<HighlighterAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setRect(const QRectF& r) { m_rect = r.normalized(); }
    void setBlend(Blend b) { m_blend = b; }
    Blend blend() const { return m_blend; }

private:
    Shape m_shape = Shape::Brush;
    Blend m_blend = Blend::Multiply;
    QRectF m_rect;
};

// 马赛克 / 高斯模糊（ANN-29）：画笔、矩形、椭圆三种形状，强度可调；效果作用于下层内容
class EffectAnnotation : public Annotation {
public:
    enum class Effect { Mosaic, Blur, Inpaint }; // Inpaint：智能擦除（ANN-30）
    enum class Shape { Brush, Rect, Ellipse };
    QString type() const override { return QStringLiteral("effect"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<EffectAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setEffect(Effect e) { m_effect = e; }
    Effect effect() const { return m_effect; }
    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setRect(const QRectF& r) { m_rect = r.normalized(); }
    QList<QPointF>& points() { return m_points; }
    void setStrength(int s) { m_strength = std::clamp(s, 1, 100); } // 1~100
    int strength() const { return m_strength; }
    QPainterPath shapePath() const;
    // 自动马赛克（ANN-31）：同组的效果一起增删；0 为不分组
    void setGroupId(int g) { m_groupId = g; }
    int groupId() const { return m_groupId; }

private:
    Effect m_effect = Effect::Mosaic;
    Shape m_shape = Shape::Rect;
    QRectF m_rect;
    QList<QPointF> m_points;
    int m_strength = 30;
    int m_groupId = 0;
};

// 文字装饰（ANN-33）：描边、背景、投影
struct TextDecor {
    bool outline = false;
    QColor outlineColor;      // 无效颜色 = 自动取与文字对比的黑 / 白
    int outlineWidth = 2;
    bool background = false;
    QColor backgroundColor = QColor(255, 255, 255, 200);
    int backgroundRadius = 4;
    int backgroundPadding = 6;
    bool shadow = false;
    QColor shadowColor = QColor(0, 0, 0, 128);
    QPointF shadowOffset = QPointF(2, 2);
    bool operator==(const TextDecor&) const = default;
    QJsonObject toJson() const;
    static TextDecor fromJson(const QJsonObject& o);
};

// 文字（ANN-32）：字体、字号、颜色、粗体、斜体；可旋转、可缩放（按包围盒缩放字号）
class TextAnnotation : public Annotation {
public:
    QString type() const override { return QStringLiteral("text"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override { m_pos += d; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter& p, const RenderContext&) const override;
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<TextAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;

    void setText(const QString& t) { m_text = t; }
    const QString& text() const { return m_text; }
    void setFont(const QFont& f) { m_font = f; }
    const QFont& font() const { return m_font; }
    void setPosition(QPointF p) { m_pos = p; } // 左上角
    QPointF position() const { return m_pos; }
    QSizeF textSize() const;
    void setDecor(const TextDecor& d) { m_decor = d; }
    const TextDecor& decor() const { return m_decor; }
    // 自动换行（ANN-34）：0 为不换行；拖动左右边改变换行宽度
    void setWrapWidth(double w) { m_wrapWidth = std::max(0.0, w); }
    double wrapWidth() const { return m_wrapWidth; }
    // 指示箭头（ANN-34）：从文字框指向目标点
    void setPointer(std::optional<QPointF> p) { m_pointer = p; }
    std::optional<QPointF> pointer() const { return m_pointer; }
    QRectF textBox() const; // 不含指示箭头的文字框
    QStringList wrappedLines() const;

private:
    double padding() const { return m_decor.background ? m_decor.backgroundPadding : 0.0; }
    QString m_text;
    QFont m_font{QStringLiteral("Microsoft YaHei"), 16};
    QPointF m_pos;
    TextDecor m_decor;
    double m_wrapWidth = 0;
    std::optional<QPointF> m_pointer;
};

// 橡皮擦（ANN-36）：画笔、矩形、椭圆形状，只擦标注不擦底图；擦除区域可二次编辑
class EraserAnnotation : public Annotation {
public:
    enum class Shape { Brush, Rect, Ellipse };
    QString type() const override { return QStringLiteral("eraser"); }
    QRectF bounds() const override;
    void setBounds(const QRectF& r) override;
    void translate(QPointF d) override;
    bool rotatable() const override { return false; }
    bool hitTest(QPointF p, double tolerance) const override;
    void paint(QPainter&, const RenderContext&) const override {} // 擦除由文档渲染时按 eraseShape 处理
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<EraserAnnotation>(*this); }
    QJsonObject toJson() const override;
    void readJson(const QJsonObject& o) override;
    QPainterPath eraseShape() const override;

    void setShape(Shape s) { m_shape = s; }
    Shape shape() const { return m_shape; }
    void setRect(const QRectF& r) { m_rect = r.normalized(); }
    QList<QPointF>& points() { return m_points; }

private:
    Shape m_shape = Shape::Brush;
    QRectF m_rect;
    QList<QPointF> m_points;
};

// 工具函数
QPainterPath brushPath(const QList<QPointF>& points, double width, bool smooth);
QJsonArray pointsToJson(const QList<QPointF>& points);
QList<QPointF> pointsFromJson(const QJsonArray& array);
QRectF boundsOfPoints(const QList<QPointF>& points, double pad);
void scalePoints(QList<QPointF>& points, const QRectF& from, const QRectF& to);

} // namespace ss
