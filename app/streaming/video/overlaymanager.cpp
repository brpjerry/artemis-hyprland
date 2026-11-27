#include "overlaymanager.h"
#include "path.h"

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xD0, 0xD0, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;

    // Artemis: the font size is chosen by setOverlayFitArea()
    m_Overlays[OverlayType::OverlayQuickMenu].color = {0x00, 0xCC, 0xCC, 0xFF};
    m_Overlays[OverlayType::OverlayQuickMenu].fontSize = 24;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    SDL_utf8strlcpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    QWriteLocker lock(&m_OverlayRendererLock);
    m_Renderer = renderer;
}

bool OverlayManager::canShowOverlay(OverlayType type)
{
    if (!isFontUsable()) {
        return false;
    }

    QReadLocker lock(&m_OverlayRendererLock);
    return m_Renderer != nullptr && m_Renderer->canShowOverlay(type);
}

bool OverlayManager::isFontUsable()
{
    // The font data doesn't change, so check it once: it has to open and
    // render text, which a damaged file can fail even when it isn't empty
    if (m_FontUsable < 0) {
        TTF_Font* font = nullptr;
        SDL_Surface* text = nullptr;
        if (!m_FontData.isEmpty() && TTF_WasInit() != 0) {
            font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()), 1, 16);
        }
        if (font != nullptr) {
            text = TTF_RenderUTF8_Blended(font, "Quick Menu", { 0xFF, 0xFF, 0xFF, 0xFF });
        }

        m_FontUsable = text != nullptr ? 1 : 0;
        if (!m_FontUsable) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "The overlay font can't be used: %s",
                        TTF_GetError());
        }

        SDL_FreeSurface(text);
        if (font != nullptr) {
            TTF_CloseFont(font);
        }
    }

    return m_FontUsable == 1;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    QReadLocker lock(&m_OverlayRendererLock);

    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        m_Overlays[type].enabled ?
            // Artemis: fitted overlays are drawn on a panel
            (m_Overlays[type].fitWidth > 0 ? renderTextPanel(type) :
            // The _Wrapped variant is required for line breaks to work
            RenderTextOutlinedWrapped(m_Overlays[type].font,
                                      m_Overlays[type].text,
                                      m_Overlays[type].color,
                                      {0, 0, 0, 255},
                                      4,
                                      1024))
            : nullptr);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

SDL_Surface* OverlayManager::RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth) {
    if (text == nullptr || text[0] == '\0') {
        return nullptr;
    }

    int oldOutline = TTF_GetFontOutline(font);
    TTF_SetFontOutline(font, outlineWidth);

    // Verify that the string won't require wrapping (which could cause the outline and the text
    // to diverge due to different wrapping positions).
    //
    // FIXME: We do this rather than just disabling wrapping entirely (wrapWidth = 0) because we
    // need further testing to ensure that all renderers can handle non-NPOT overlay textures.
    for (const QString& line : QString(text).split('\n')) {
        int extent, count;
        if (TTF_MeasureUTF8(font, line.toUtf8(), wrapWidth, &extent, &count) == 0 && count < line.size()) {
            // If it requires wrapping, render it without the outline
            TTF_SetFontOutline(font, oldOutline);
            return TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
        }
    }

    // Draw text twice, but outline is a bit bigger
    auto outlineSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, outlineColor, wrapWidth);
    TTF_SetFontOutline(font, 0);
    auto textSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
    TTF_SetFontOutline(font, oldOutline);

    if (outlineSurface == nullptr || textSurface == nullptr) {
        SDL_FreeSurface(outlineSurface);
        SDL_FreeSurface(textSurface);
        return nullptr;
    }

    // Merge the texts
    SDL_Rect dst = { outlineWidth, outlineWidth, textSurface->w, textSurface->h };
    SDL_BlitSurface(textSurface, nullptr, outlineSurface, &dst);

    SDL_FreeSurface(textSurface);
    return outlineSurface;
}

// Artemis: fitted text panels (the quick menu)

namespace {

constexpr int kPanelMinFontSize = 8;
constexpr int kPanelBorder = 2;

int panelPaddingX(int fontSize)
{
    return fontSize;
}

int panelPaddingY(int fontSize)
{
    return fontSize / 2;
}

int panelWidth(int textWidth, int fontSize)
{
    return textWidth + 2 * (panelPaddingX(fontSize) + kPanelBorder);
}

int panelHeight(int textHeight, int fontSize)
{
    return textHeight + 2 * (panelPaddingY(fontSize) + kPanelBorder);
}

bool measureLines(TTF_Font* font, const QList<QByteArray>& lines, int* width, int* height)
{
    int maxWidth = 0;
    for (const QByteArray& line : lines) {
        if (line.isEmpty()) {
            continue;
        }

        int lineWidth, lineHeight;
        if (TTF_SizeUTF8(font, line.constData(), &lineWidth, &lineHeight) != 0) {
            return false;
        }
        maxWidth = qMax(maxWidth, lineWidth);
    }

    *width = maxWidth;
    *height = TTF_FontLineSkip(font) * (int)lines.size();
    return true;
}

}

void OverlayManager::setOverlayFitArea(OverlayType type, int maxWidth, int maxHeight, int maxFontSize)
{
    auto& overlay = m_Overlays[type];
    if (overlay.fitWidth != maxWidth || overlay.fitHeight != maxHeight || overlay.maxFontSize != maxFontSize) {
        overlay.fitWidth = maxWidth;
        overlay.fitHeight = maxHeight;
        overlay.maxFontSize = maxFontSize;

        // Fit the font again on the next update
        overlay.fittedLines = 0;
        overlay.fittedColumns = 0;
    }
}

bool OverlayManager::openOverlayFont(OverlayType type, int fontSize)
{
    auto& overlay = m_Overlays[type];
    if (overlay.font != nullptr && overlay.fontSize == fontSize) {
        return true;
    }

    if (overlay.font != nullptr) {
        TTF_CloseFont(overlay.font);
        overlay.font = nullptr;
    }
    overlay.fontSize = fontSize;

    if (m_FontData.isEmpty()) {
        return false;
    }

    // m_FontData must stay around until the font is closed
    overlay.font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                  1,
                                  fontSize);
    if (overlay.font == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_OpenFont() failed: %s",
                    TTF_GetError());
        return false;
    }

    return true;
}

SDL_Surface* OverlayManager::renderTextPanel(OverlayType type)
{
    auto& overlay = m_Overlays[type];
    if (overlay.text[0] == 0) {
        return nullptr;
    }

    const QList<QByteArray> lines = QByteArray(overlay.text).split('\n');
    int columns = 0;
    for (const QByteArray& line : lines) {
        columns = qMax(columns, (int)line.size());
    }

    const int maxFontSize = qMax(kPanelMinFontSize, overlay.maxFontSize);
    int fontSize = qBound(kPanelMinFontSize, overlay.font != nullptr ? overlay.fontSize : maxFontSize, maxFontSize);
    int textWidth, textHeight;
    if (!openOverlayFont(type, fontSize) || !measureLines(overlay.font, lines, &textWidth, &textHeight)) {
        return nullptr;
    }

    // Fit the font size when the area or the text's shape changed. Keeping
    // it otherwise avoids reopening the font on every update, and the menu's
    // text from resizing as the selection moves.
    if (overlay.fittedLines != (int)lines.size() || overlay.fittedColumns != columns) {
        // The overlay font is monospaced, so text scales with the font size
        // and one measurement estimates the size that fills the area. The
        // fixed border makes that an underestimate when growing and at most
        // slightly over when shrinking, which the loop below corrects.
        const double scale = qMin((double)overlay.fitWidth / panelWidth(textWidth, fontSize),
                                  (double)overlay.fitHeight / panelHeight(textHeight, fontSize));
        const int estimate = qBound(kPanelMinFontSize, (int)(fontSize * scale), maxFontSize);
        if (estimate != fontSize) {
            fontSize = estimate;
            if (!openOverlayFont(type, fontSize) || !measureLines(overlay.font, lines, &textWidth, &textHeight)) {
                return nullptr;
            }
        }

        while (fontSize > kPanelMinFontSize &&
               (panelWidth(textWidth, fontSize) > overlay.fitWidth ||
                panelHeight(textHeight, fontSize) > overlay.fitHeight)) {
            fontSize--;
            if (!openOverlayFont(type, fontSize) || !measureLines(overlay.font, lines, &textWidth, &textHeight)) {
                return nullptr;
            }
        }

        overlay.fittedLines = (int)lines.size();
        overlay.fittedColumns = columns;
    }

    const int width = panelWidth(textWidth, fontSize);
    const int height = panelHeight(textHeight, fontSize);
    SDL_Surface* panel = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (panel == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateRGBSurfaceWithFormat() failed: %s",
                     SDL_GetError());
        return nullptr;
    }

    // A border in the overlay's color around a dark, translucent backdrop
    const SDL_Color color = overlay.color;
    SDL_FillRect(panel, nullptr, SDL_MapRGBA(panel->format, color.r, color.g, color.b, 0xFF));
    SDL_Rect backdrop = { kPanelBorder, kPanelBorder, width - 2 * kPanelBorder, height - 2 * kPanelBorder };
    SDL_FillRect(panel, &backdrop, SDL_MapRGBA(panel->format, 0x00, 0x00, 0x00, 0xD8));

    const int lineSkip = TTF_FontLineSkip(overlay.font);
    for (int i = 0; i < (int)lines.size(); i++) {
        const QByteArray& line = lines.at(i);
        if (line.isEmpty()) {
            continue;
        }

        const int y = kPanelBorder + panelPaddingY(fontSize) + i * lineSkip;

        // The selected row: white text on a bar in a darker shade of the color
        const bool selected = line.startsWith('>');
        if (selected) {
            SDL_Rect bar = { kPanelBorder, y, width - 2 * kPanelBorder, lineSkip };
            SDL_FillRect(panel, &bar, SDL_MapRGBA(panel->format, color.r / 3, color.g / 3, color.b / 3, 0xE0));
        }

        SDL_Surface* lineSurface = TTF_RenderUTF8_Blended(overlay.font, line.constData(),
                                                          selected ? SDL_Color { 0xFF, 0xFF, 0xFF, 0xFF } : color);
        if (lineSurface == nullptr) {
            continue;
        }

        SDL_Rect dst = { kPanelBorder + panelPaddingX(fontSize), y, 0, 0 };
        SDL_BlitSurface(lineSurface, nullptr, panel, &dst);
        SDL_FreeSurface(lineSurface);
    }

    return panel;
}
