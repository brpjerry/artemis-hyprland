#pragma once

#include <QString>
#include <QReadWriteLock>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayQuickMenu, // Artemis: centered, see setOverlayFitArea()
    OverlayMax
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;

    virtual void notifyOverlayUpdated(OverlayType type) = 0;

    // Artemis: whether this renderer can display this overlay right now
    virtual bool canShowOverlay(OverlayType) { return true; }
};

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    bool isOverlayEnabled(OverlayType type);
    char* getOverlayText(OverlayType type);
    void updateOverlayText(OverlayType type, const char* text);
    int getOverlayMaxTextLength();
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Color getOverlayColor(OverlayType type);
    int getOverlayFontSize(OverlayType type);
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    void setOverlayRenderer(IOverlayRenderer* renderer);

    // Artemis: whether the current renderer can display this overlay (false
    // while there's no renderer, or no font to draw text with)
    bool canShowOverlay(OverlayType type);

    // Artemis: draws this overlay's text on a backdrop panel, line by line and
    // without wrapping. The font size is estimated up to maxFontSize and then
    // stepped down until the panel fits within maxWidth x maxHeight pixels or
    // the size reaches 8, so a very small window can clip the panel. A line
    // starting with '>' is highlighted. Call it on the thread that updates
    // the overlay's text.
    void setOverlayFitArea(OverlayType type, int maxWidth, int maxHeight, int maxFontSize);

    // Artemis: whether the last update of a fitted overlay failed to draw its
    // panel (say, out of memory). Clears the failure. Renderers can still
    // fail to show a panel that was drawn; this doesn't cover that.
    bool takeOverlayPanelFailure(OverlayType type);

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth);
    bool openOverlayFont(OverlayType type, int fontSize);
    SDL_Surface* renderTextPanel(OverlayType type);
    SDL_Surface* drawTextPanel(OverlayType type);
    bool isFontUsable();

    struct {
        bool enabled;
        int fontSize;
        SDL_Color color;
        char text[1024];

        TTF_Font* font;
        SDL_Surface* surface;

        // setOverlayFitArea(); unused while fitWidth is 0
        int fitWidth;
        int fitHeight;
        int maxFontSize;
        int fittedLines; // Text shape fontSize was last fitted for
        int fittedColumns;
        bool panelFailed; // takeOverlayPanelFailure()
    } m_Overlays[OverlayMax];
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;
    int m_FontUsable = -1; // Artemis: isFontUsable(), -1 until checked
    QReadWriteLock m_OverlayRendererLock;
};

}
