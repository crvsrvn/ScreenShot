#include "testing/FakeScreenCapture.h"

#include "core/util/Clock.h"

namespace ss::testing {

FakeScreenCapture::FakeScreenCapture(QList<ScreenInfo> screens)
{
    setScreens(std::move(screens));
}

void FakeScreenCapture::setScreens(QList<ScreenInfo> screens)
{
    m_screens = std::move(screens);
    m_images.clear();
    m_failing.clear();
    for (int i = 0; i < m_screens.size(); ++i) {
        m_images.append(QImage());
        m_failing.append(false);
    }
}

void FakeScreenCapture::setImage(int index, QImage image)
{
    if (index >= 0 && index < m_images.size())
        m_images[index] = std::move(image);
}

void FakeScreenCapture::setFailing(int index, bool failing)
{
    if (index >= 0 && index < m_failing.size())
        m_failing[index] = failing;
}

std::optional<ScreenSnapshot> FakeScreenCapture::capture(const ScreenInfo& screen)
{
    const int index = static_cast<int>(m_screens.indexOf(screen));
    if (index < 0 || m_failing[index])
        return std::nullopt;
    ++m_captureCount;
    if (m_images[index].isNull())
        m_images[index] = patternImage(index, screen.physicalGeometry.size());
    if (!qFuzzyCompare(m_images[index].devicePixelRatio(), screen.scale))
        m_images[index].setDevicePixelRatio(screen.scale);
    return ScreenSnapshot{screen, m_images[index], monotonicNs()};
}

QRgb FakeScreenCapture::patternPixel(int screenIndex, int x, int y)
{
    const int r = x & 0xFF;
    const int g = y & 0xFF;
    const int b = (screenIndex * 37 + (x >> 8) * 5 + (y >> 8) * 11) & 0xFF;
    return qRgb(r, g, b);
}

QImage FakeScreenCapture::patternImage(int screenIndex, QSize size)
{
    QImage img(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
            line[x] = patternPixel(screenIndex, x, y);
    }
    return img;
}

} // namespace ss::testing
