#ifndef __LEVELSELECTOR_H__
#define __LEVELSELECTOR_H__

#include "widget/Widget.h"
#include "widget/ButtonListener.h"
#include "ConstEnums.h"

class LawnApp;
class LawnStoneButton;

namespace Sexy {
    class Graphics;
    class Image;
}

class LevelSelector : public Sexy::Widget, public Sexy::ButtonListener {
public:
    enum ViewState {
        VIEW_WORLD_MAP,
        VIEW_WORLD_PORTAL
    };

    enum {
        LevelSelector_Back = 100,
        LevelSelector_Prev,
        LevelSelector_Next,
        LevelSelector_Play
    };

    LawnApp* mApp;
    ViewState mViewState;
    int mSelectedWorld; // 0: Day, 1: Night, 2: Pool, 3: Fog, 4: Roof
    int mSelectedLevel; // 1 to 10 within that world

    Sexy::Image* mBgMap;
    Sexy::Image* mWorldImages[5]; // day, night, pool, fog, roof
    Sexy::Image* mPortalImage;    // day-portal.jpg
    Sexy::Image* mLevelBgImages[5]; // day-level, night-level, etc.
    
    float           mScrollX;           // Current animated scroll position
    float           mTargetScrollX;     // Target scroll position to snap to
    int             mDragStartX;        // Initial X when dragging started
    float           mDragStartScrollX;  // ScrollX when dragging started
    bool            mIsDragging;
    bool            mIsClick;           // To distinguish between click and drag

    LawnStoneButton* mBackButton;

    LevelSelector(LawnApp* theApp);
    virtual ~LevelSelector();

    virtual void Update() override;
    virtual void Draw(Sexy::Graphics* g) override;
    virtual void KeyDown(Sexy::KeyCode theKey) override;
    virtual void MouseDown(int x, int y, int theClickCount) override;
    virtual void MouseDrag(int x, int y) override;
    virtual void MouseUp(int x, int y, int theClickCount) override;
    virtual void MouseMove(int x, int y) override;

    void SyncButtons();
    void DrawPlantIcon(Sexy::Graphics* g, SeedType theSeedType, float x, float y, float theScale);
    void DrawCard(Sexy::Graphics* g, int theLevel, int x, int y, float scale);
    void GoToWorld(int theWorld);
    void StartSelectedLevel();

    // ButtonListener methods
    virtual void ButtonPress(int theId) override;
    virtual void ButtonDepress(int theId) override;
    virtual void ButtonDownTick(int theId) override;
    virtual void ButtonMouseEnter(int theId) override;
    virtual void ButtonMouseLeave(int theId) override;
    virtual void ButtonMouseMove(int theId, int theX, int theY) override;
};

#endif
