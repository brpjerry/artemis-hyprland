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

    // Artemis: draws this overlay's text on a backdrop panel, line by line and
    // without wrapping, at the largest font size up to maxFontSize that keeps
    // the panel within maxWidth x maxHeight pixels. A line starting with '>'
    // is highlighted. Call it on the thread that updates the overlay's text.
    void setOverlayFitArea(OverlayType type, int maxWidth, int maxHeight, int maxFontSize);

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth);
    bool openOverlayFont(OverlayType type, int fontSize);
    SDL_Surface* renderTextPanel(OverlayType type);

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
    } m_Overlays[OverlayMax];
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;
    QReadWriteLock m_OverlayRendererLock;
};

}
