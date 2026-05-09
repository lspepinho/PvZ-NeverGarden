#include "LevelSelector.h"
#include "../../LawnApp.h"
#include "../System/PlayerInfo.h"
#include "../../Resources.h"
#include "../../GameConstants.h"
#include "GameButton.h"
#include "graphics/Graphics.h"
#include "graphics/Image.h"
#include "graphics/Font.h"
#include "widget/WidgetManager.h"
#include "graphics/GLImage.h"
#include "SexyAppFramework/misc/ResourceManager.h"
#include "Sexy.TodLib/TodCommon.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/Plant.h"

using namespace Sexy;

static int gMenuTextX = -8;
static int gMenuTextY = 0;

// Base Resolutions
const float BASE_W = 1535.0f;
const float BASE_H = 688.0f;
const float CARD_BASE_W = 388.0f;
const float CARD_BASE_H = 572.0f;

// Helper to try loading image from various possible paths
static Image* TryLoadImage(LawnApp* theApp, const std::string& theName, const std::string& theUuid = "") {
    Image* anImg = theApp->GetImage("assets/" + theName);
    if (!anImg) anImg = theApp->GetImage(theName);
    if (!anImg && !theUuid.empty()) {
        anImg = theApp->GetImage("assets/" + theUuid);
        if (!anImg) anImg = theApp->GetImage(theUuid);
    }
    return anImg;
}

struct CollisionZone {
    int x, y, w, h;
    const char* label;
};

// World selector tombstones - OLD OFFSETS (day.jpg)
static CollisionZone gWorldButtonsOld[] = {
    {429, 452, 137, 189, "Day"},
    {566, 442, 127, 124, "Night"},
    {693, 435, 127, 75, "Pool"},
    {818, 373, 151, 64, "Fog"},
    {949, 442, 123, 39, "Roof"}
};

// World selector tombstones - NEW OFFSETS (day-portal.jpg and beyond)
static CollisionZone gWorldButtonsNew[] = {
    {340, 493, 143, 139, "Day"}, {350, 470, 123, 31, "Day"}, {371, 453, 81, 31, "Day"},
    {554, 509, 158, 69, "Night"}, {568, 445, 131, 68, "Night"},
    {699, 439, 121, 70, "Pool"}, {818, 441, 23, 31, "Pool"}, {716, 511, 100, 29, "Pool"}, {728, 403, 86, 40, "Pool"},
    {820, 362, 148, 81, "Fog"}, {849, 445, 89, 18, "Fog"}, {841, 464, 106, 29, "Fog"}, {822, 472, 19, 21, "Fog"}, {822, 497, 133, 62, "Fog"},
    {992, 418, 44, 27, "Roof"}, {968, 443, 93, 27, "Roof"}, {953, 464, 133, 35, "Roof"}, {955, 503, 125, 8, "Roof"}, {959, 513, 75, 71, "Roof"}, {1034, 518, 13, 41, "Roof"}
};

LevelSelector::LevelSelector(LawnApp* theApp) : mApp(theApp) {
    mViewState = VIEW_WORLD_MAP;
    mSelectedWorld = 0;
    mSelectedLevel = 1;
    
    // Load Assets
    mBgMap = TryLoadImage(mApp, "stone.jpg", "20488b51-d9ef-4f8b-9485-d5f689f85302.jpg");
    // Load World Images
    mWorldImages[0] = TryLoadImage(mApp, "day.jpg", "4d65912d-bd35-4c4d-9fca-dcef7cb736b7.jpg");
    mWorldImages[1] = TryLoadImage(mApp, "night.jpg", "ca00a0cd-f13d-43bf-8949-6cb43c99f9a2.jpg");
    mWorldImages[2] = TryLoadImage(mApp, "pool.jpg", "b7017ae1-a37f-4dc7-8431-463a30bc39b2.jpg");
    mWorldImages[3] = TryLoadImage(mApp, "fog.jpg", "8df2ae30-e91d-4e6b-b899-8575b4784028.jpg");
    mWorldImages[4] = TryLoadImage(mApp, "roof.jpg", "adc76a28-8191-46c3-92dc-d419f69544bb.jpg");
    mPortalImage    = TryLoadImage(mApp, "day-portal.jpg"); // Pre-load portal

    if (mApp->mPlayerInfo) {
        // --- PERSISTENT PROGRESS PROTECTION ---
        int currentLevel = mApp->mPlayerInfo->mLevel;
        int maxLevelBackup = (int)mApp->mPlayerInfo->mChallengeRecords[99];
        
        if (maxLevelBackup > currentLevel) {
            currentLevel = maxLevelBackup;
            mApp->mPlayerInfo->mLevel = currentLevel; 
        } else {
            mApp->mPlayerInfo->mChallengeRecords[99] = (uint32_t)currentLevel;
        }

        if (currentLevel > 50) currentLevel = 50; 
        
        mSelectedWorld = (currentLevel - 1) / 10;
        mSelectedLevel = (currentLevel - 1) % 10 + 1;
    }
    
    mLevelBgImages[0] = TryLoadImage(mApp, "day-level.png"); 
    mLevelBgImages[1] = TryLoadImage(mApp, "night-level.png"); 
    mLevelBgImages[2] = TryLoadImage(mApp, "pool-level.png"); 
    mLevelBgImages[3] = TryLoadImage(mApp, "fog-level.png"); 
    mLevelBgImages[4] = TryLoadImage(mApp, "roof-level.png"); 

    mWidth = BOARD_WIDTH;
    mHeight = BOARD_HEIGHT;
    mHasAlpha = true;

    mBackButton = MakeButton(LevelSelector_Back, this, "MENU"); 
    AddWidget(mBackButton);

    mScrollX = mSelectedLevel * 200.0f;
    mTargetScrollX = mScrollX;
    mIsDragging = false;
    mIsClick = false;

    SyncButtons();
    mApp->mWidgetManager->SetFocus(this);
}

LevelSelector::~LevelSelector() {
}

void LevelSelector::SyncButtons() {
    // Back button top left
    mBackButton->Resize(20, 20, 130, 45);
    mBackButton->mTextOffsetX = gMenuTextX;
    mBackButton->mTextOffsetY = gMenuTextY;
    
    MarkDirty();
}

void LevelSelector::Update() {
    // Smooth Lerp for the slide animation
    float lerpFactor = mIsDragging ? 0.4f : 0.15f;
    mScrollX += (mTargetScrollX - mScrollX) * lerpFactor;
    
    if (!mIsDragging && std::abs(mTargetScrollX - mScrollX) < 0.5f) {
        mScrollX = mTargetScrollX;
    }

    MarkDirty();
}

void LevelSelector::DrawPlantIcon(Graphics* g, SeedType theSeedType, float x, float y, float theScale) {
    float aScale = 0.5f;
    float aOffsetX = 5.0f;
    float aOffsetY = 8.0f;
    
    switch (theSeedType) {
        case SeedType::SEED_TALLNUT: aScale = 0.3f; aOffsetX = 12.0f; aOffsetY = 22.0f; break;
        case SeedType::SEED_INSTANT_COFFEE: aScale = 0.55f; aOffsetX = 0; aOffsetY = 9.0f; break;
        case SeedType::SEED_COBCANNON: aScale = 0.26f; aOffsetX = 6.0f; aOffsetY = 22.0f; break;
        case SeedType::SEED_CACTUS: aOffsetX = 9.0f; aOffsetY = 13.0f; break;
        case SeedType::SEED_POTATOMINE: aScale = 0.4f; aOffsetX = 8.0f; aOffsetY = 12.0f; break;
        case SeedType::SEED_MAGNETSHROOM: aOffsetY = 12.0f; break;
        case SeedType::SEED_FUMESHROOM:
        case SeedType::SEED_PUMPKINSHELL:
        case SeedType::SEED_CHOMPER:
        case SeedType::SEED_DOOMSHROOM:
        case SeedType::SEED_SQUASH:
        case SeedType::SEED_HYPNOSHROOM:
        case SeedType::SEED_SPIKEWEED:
        case SeedType::SEED_SPIKEROCK:
        case SeedType::SEED_PLANTERN:
        case SeedType::SEED_TORCHWOOD:
        case SeedType::SEED_TANGLEKELP: aScale = 0.4f; aOffsetX = 8.0f; aOffsetY = 12.0f; break;
        case SeedType::SEED_TWINSUNFLOWER:
        case SeedType::SEED_GLOOMSHROOM: aScale = 0.45f; aOffsetX = 7.0f; aOffsetY = 14.0f; break;
        case SeedType::SEED_CATTAIL: aScale = 0.45f; aOffsetX = 8.0f; aOffsetY = 13.0f; break;
        case SeedType::SEED_UMBRELLA: aScale = 0.5f; aOffsetX = 5.0f; aOffsetY = 10.0f; break;
        case SeedType::SEED_KERNELPULT: aScale = 0.4f; aOffsetX = 13.0f; aOffsetY = 14.0f; break;
        case SeedType::SEED_CABBAGEPULT: aScale = 0.4f; aOffsetX = 15.0f; aOffsetY = 14.0f; break;
        case SeedType::SEED_MELONPULT:
        case SeedType::SEED_WINTERMELON: aScale = 0.35f; aOffsetX = 18.0f; aOffsetY = 19.0f; break;
        case SeedType::SEED_GRAVEBUSTER: aScale = 0.4f; aOffsetX = 10.0f; aOffsetY = 15.0f; break;
        case SeedType::SEED_SPLITPEA: aScale = 0.45f; aOffsetX = 12.0f; aOffsetY = 12.0f; break;
        case SeedType::SEED_BLOVER: aScale = 0.4f; aOffsetX = 8.0f; aOffsetY = 17.0f; break;
        case SeedType::SEED_STARFRUIT: aScale = 0.5f; aOffsetX = 6.0f; aOffsetY = 8.0f; break;
        case SeedType::SEED_THREEPEATER: aScale = 0.5f; aOffsetX = 5.0f; aOffsetY = 10.0f; break;
        case SeedType::SEED_GATLINGPEA: aScale = 0.5f; aOffsetX = 2.0f; aOffsetY = 8.0f; break;
        default: break;
    }

    Graphics aSeedG(*g);
    aSeedG.mScaleX = theScale * aScale;
    aSeedG.mScaleY = theScale * aScale;
    Plant::DrawSeedType(&aSeedG, theSeedType, SeedType::SEED_NONE, DrawVariation::VARIATION_NORMAL, x + aOffsetX * theScale, y + aOffsetY * theScale);
}

void LevelSelector::DrawCard(Graphics* g, int theLevel, int x, int y, float scale) {
    int sw = (int)(180 * scale);
    int sh = (int)(265 * scale);
    int sx = x - sw / 2;
    int sy = y - sh / 2;

    if (mLevelBgImages[mSelectedWorld]) {
        g->DrawImage(mLevelBgImages[mSelectedWorld], sx, sy, sw, sh);
    }

    float internalScale = (float)sw / CARD_BASE_W;

    // --- EXACT COORDINATES FROM COLLISION DATA (Calculated Centers) ---
    float plantCX = sx + 196.5f * internalScale;
    float plantCY = sy + 453.5f * internalScale;
    float zombieCX = sx + 261.5f * internalScale;
    float zombieCY = sy + 192.0f * internalScale;
    float textCX = sx + 195.5f * internalScale;
    float textCY = sy + 337.0f * internalScale;

    // --- MANUAL AWARD MAPPING ---
    SeedType aSeedType = SeedType::SEED_NONE;
    int specialIdx = 0; // 1=Shovel, 2=Taco, 3=Key, 4=Note, 5=Trophy

    if (mSelectedWorld == 0) { // Day
        static SeedType awards[] = {SEED_SUNFLOWER, SEED_CHERRYBOMB, SEED_WALLNUT, SEED_POTATOMINE, SEED_NONE, SEED_SNOWPEA, SEED_CHOMPER, SEED_REPEATER, SEED_NONE, SEED_PUFFSHROOM};
        aSeedType = awards[theLevel - 1];
        if (theLevel == 5) specialIdx = 1;
        else if (theLevel == 9) specialIdx = 4;
    } else if (mSelectedWorld == 1) { // Night
        static SeedType awards[] = {SEED_SUNSHROOM, SEED_FUMESHROOM, SEED_GRAVEBUSTER, SEED_HYPNOSHROOM, SEED_NONE, SEED_SCAREDYSHROOM, SEED_ICESHROOM, SEED_DOOMSHROOM, SEED_NONE, SEED_LILYPAD};
        aSeedType = awards[theLevel - 1];
        if (theLevel == 5) specialIdx = 2;
        else if (theLevel == 9) specialIdx = 4;
    } else if (mSelectedWorld == 2) { // Pool
        static SeedType awards[] = {SEED_SQUASH, SEED_THREEPEATER, SEED_TANGLEKELP, SEED_JALAPENO, SEED_NONE, SEED_SPIKEWEED, SEED_TORCHWOOD, SEED_TALLNUT, SEED_NONE, SEED_SEASHROOM};
        aSeedType = awards[theLevel - 1];
        if (theLevel == 5) specialIdx = 3;
        else if (theLevel == 9) specialIdx = 4;
    } else if (mSelectedWorld == 3) { // Fog
        static SeedType awards[] = {SEED_PLANTERN, SEED_CACTUS, SEED_BLOVER, SEED_SPLITPEA, SEED_NONE, SEED_STARFRUIT, SEED_PUMPKINSHELL, SEED_MAGNETSHROOM, SEED_NONE, SEED_CABBAGEPULT};
        aSeedType = awards[theLevel - 1];
        if (theLevel == 5) specialIdx = 2;
        else if (theLevel == 9) specialIdx = 4;
    } else if (mSelectedWorld == 4) { // Roof
        static SeedType awards[] = {SEED_FLOWERPOT, SEED_KERNELPULT, SEED_INSTANT_COFFEE, SEED_GARLIC, SEED_NONE, SEED_UMBRELLA, SEED_MARIGOLD, SEED_MELONPULT, SEED_NONE, SEED_NONE};
        aSeedType = awards[theLevel - 1];
        if (theLevel == 5) specialIdx = 2;
        else if (theLevel == 8 || theLevel == 9) specialIdx = 4;
        else if (theLevel == 10) specialIdx = 5;
    }

    // Draw Reward
    if (aSeedType != SeedType::SEED_NONE && specialIdx == 0) {
        // Draw the plant using the game's premium rendering logic but WITHOUT the background packet
        // Final verified offsets: X=-23.0, Y=-29.0
        DrawPlantIcon(g, aSeedType, plantCX - 23.0f * internalScale * 2.5f, plantCY - 29.0f * internalScale * 2.5f, internalScale * 2.5f);
    } else if (specialIdx != 0) {
        float specialScale = internalScale * 1.8f;
        Image* aImg = nullptr;
        if (specialIdx == 1) aImg = Sexy::IMAGE_SHOVEL;
        else if (specialIdx == 2) aImg = Sexy::IMAGE_TACO;
        else if (specialIdx == 3) aImg = Sexy::IMAGE_CARKEYS;
        else if (specialIdx == 4) aImg = Sexy::IMAGE_ZOMBIE_NOTE_SMALL;
        else if (specialIdx == 5) aImg = Sexy::IMAGE_TROPHY;
        
        if (aImg) {
            // Final verified item offsets: X=-85.0, Y=-83.0
            TodDrawImageCenterScaledF(g, aImg, plantCX - 85.0f * internalScale, plantCY - 83.0f * internalScale, specialScale, specialScale);
        }
    }

    // --- FULL BODY ZOMBIE ---
    Image* aZombieImg = Sexy::IMAGE_TROPHY;
    if (aZombieImg) {
        TodDrawImageCenterScaledF(g, aZombieImg, zombieCX, zombieCY, internalScale * 1.4f, internalScale * 1.4f);
    }

    // --- LEVEL NAME ---
    _Font* aFont = FONT_BRIANNETOD12;
    if (aFont) {
        g->SetFont(aFont);
        g->SetColor(Color::White);
        char txt[32];
        sprintf(txt, "Level %d-%d", mSelectedWorld + 1, theLevel);
        
        float textScale = 0.85f; 
        int tw = (int)(aFont->StringWidth(txt) * textScale);
        
        g->DrawString(txt, (int)(textCX - tw / 2), (int)(textCY + aFont->GetAscent() * 0.3f));
    }

    // Lock if not reached
    int playerLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
    int levelInWorld = mSelectedWorld * 10 + theLevel;
    if (levelInWorld > playerLevel) {
        g->SetColor(Color(0, 0, 0, 180));
        g->FillRect(sx + 10, sy + 10, sw - 20, sh - 20);
    }
}

void LevelSelector::Draw(Graphics* g) {
    float scaleX = (float)mWidth / BASE_W;
    float scaleY = (float)mHeight / BASE_H;

    // 1. Draw Background
    if (mBgMap) g->DrawImage(mBgMap, 0, 0, mWidth, mHeight);

    // 2. Draw World Overlays (Tombstones)
    int playerLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
    if (mSelectedWorld == 0 && playerLevel > 10 && mPortalImage) {
        g->DrawImage(mPortalImage, 0, 0, mWidth, mHeight);
    } else if (mWorldImages[mSelectedWorld]) {
        g->DrawImage(mWorldImages[mSelectedWorld], 0, 0, mWidth, mHeight);
    }

    // 3. Draw Level Cards
    for (int i = 1; i <= 10; i++) {
        float basePos = i * 200.0f;
        float offset = basePos - mScrollX;
        
        // Culling: Only draw if roughly on screen
        if (std::abs(offset) > mWidth / 2 + 150) continue;

        // Dynamic scale based on distance to center
        float dist = std::abs(offset);
        float scale = 1.0f - (dist / 600.0f);
        if (scale < 0.6f) scale = 0.6f;
        if (scale > 1.0f) scale = 1.0f;

        DrawCard(g, i, mWidth / 2 + (int)offset, 180, scale);
    }
}

void LevelSelector::MouseMove(int x, int y) {
    float scaleX = (float)mWidth / BASE_W;
    float scaleY = (float)mHeight / BASE_H;
    bool hover = false;

    // Check Cards
    for (int i = 1; i <= 10; i++) {
        if (abs(i - mSelectedLevel) > 3) continue;
        float offset = (i - mSelectedLevel) * 200.0f;
        float scale = (i == mSelectedLevel) ? 1.0f : 0.7f;
        int sw = (int)(180 * scale);
        int sh = (int)(265 * scale);
        int cx = mWidth / 2 + (int)offset;
        int cy = 180;
        if (x >= cx - sw/2 && x < cx + sw/2 && y >= cy - sh/2 && y < cy + sh/2) {
            hover = true;
            break;
        }
    }

    // Check Tombstones
    if (!hover) {
        int playerLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
        bool useNewOffsets = (playerLevel > 10);
        int zoneCount = useNewOffsets ? (sizeof(gWorldButtonsNew)/sizeof(CollisionZone)) : (sizeof(gWorldButtonsOld)/sizeof(CollisionZone));
        const CollisionZone* zones = useNewOffsets ? gWorldButtonsNew : gWorldButtonsOld;

        for (int i = 0; i < zoneCount; i++) {
            const auto& zone = zones[i];
            int sx = (int)(zone.x * scaleX);
            int sy = (int)(zone.y * scaleY);
            int sw = (int)(zone.w * scaleX);
            int sh = (int)(zone.h * scaleY);
            if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
                // Check if world is unlocked
                int targetWorld = -1;
                std::string lbl = zone.label;
                if (lbl == "Day") targetWorld = 0;
                else if (lbl == "Night") targetWorld = 1;
                else if (lbl == "Pool") targetWorld = 2;
                else if (lbl == "Fog") targetWorld = 3;
                else if (lbl == "Roof") targetWorld = 4;

                int playerMaxLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
                int worldOfMax = (playerMaxLevel - 1) / 10;
                if (targetWorld != -1 && targetWorld <= worldOfMax) {
                    hover = true;
                }
                break;
            }
        }
    }

    if (hover) mApp->SetCursor(Sexy::CURSOR_HAND);
    else mApp->SetCursor(Sexy::CURSOR_POINTER);
}

void LevelSelector::MouseDown(int x, int y, int theClickCount) {
    float scaleX = (float)mWidth / BASE_W;
    float scaleY = (float)mHeight / BASE_H;

    // Start Dragging
    mIsDragging = true;
    mIsClick = true;
    mDragStartX = x;
    mDragStartScrollX = mTargetScrollX;

    // World Selection via tombstones
    int playerLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
    bool useNewOffsets = (playerLevel > 10);
    
    int zoneCount = useNewOffsets ? (sizeof(gWorldButtonsNew)/sizeof(CollisionZone)) : (sizeof(gWorldButtonsOld)/sizeof(CollisionZone));
    const CollisionZone* zones = useNewOffsets ? gWorldButtonsNew : gWorldButtonsOld;

    for (int i = 0; i < zoneCount; i++) {
        const auto& zone = zones[i];
        int sx = (int)(zone.x * scaleX);
        int sy = (int)(zone.y * scaleY);
        int sw = (int)(zone.w * scaleX);
        int sh = (int)(zone.h * scaleY);

        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
            // Map Label to World Index
            int targetWorld = -1;
            std::string lbl = zone.label;
            if (lbl == "Day") targetWorld = 0;
            else if (lbl == "Night") targetWorld = 1;
            else if (lbl == "Pool") targetWorld = 2;
            else if (lbl == "Fog") targetWorld = 3;
            else if (lbl == "Roof") targetWorld = 4;

            if (targetWorld != -1) {
                int playerMaxLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
                int worldOfMax = (playerMaxLevel - 1) / 10;
                if (targetWorld <= worldOfMax) {
                    mSelectedWorld = targetWorld;
                    mSelectedLevel = 1;
                    if (targetWorld == worldOfMax) mSelectedLevel = (playerMaxLevel - 1) % 10 + 1;
                    
                    mTargetScrollX = mSelectedLevel * 200.0f;
                    SyncButtons();
                }
                return;
            }
        }
    }

    Widget::MouseDown(x, y, theClickCount);
}

void LevelSelector::MouseDrag(int x, int y) {
    if (mIsDragging) {
        int diff = x - mDragStartX;
        if (std::abs(diff) > 10) mIsClick = false;
        
        mTargetScrollX = mDragStartScrollX - diff;
    }
}

void LevelSelector::MouseUp(int x, int y, int theClickCount) {
    if (!mIsDragging) return;
    mIsDragging = false;

    if (mIsClick) {
        // Handle as a standard click (Start Level or Snap to card)
        for (int i = 1; i <= 10; i++) {
            float basePos = i * 200.0f;
            float offset = basePos - mScrollX;
            float scale = (std::abs(offset) < 50) ? 1.0f : 0.7f;
            int sw = (int)(180 * scale);
            int sh = (int)(265 * scale);
            int cx = mWidth / 2 + (int)offset;
            int cy = 180;
            
            if (x >= cx - sw/2 && x < cx + sw/2 && y >= cy - sh/2 && y < cy + sh/2) {
                if (std::abs(offset) < 50) { // Center card clicked
                    int levelToStart = mSelectedWorld * 10 + mSelectedLevel;
                    if (mApp->mPlayerInfo) {
                        if (mApp->mPlayerInfo->mLevel > (int)mApp->mPlayerInfo->mChallengeRecords[99]) {
                            mApp->mPlayerInfo->mChallengeRecords[99] = (uint32_t)mApp->mPlayerInfo->mLevel;
                        }
                        mApp->mPlayerInfo->mLevel = levelToStart;
                    }
                    mApp->KillLevelSelector();
                    mApp->PreNewGame(GameMode::GAMEMODE_ADVENTURE, false);
                } else {
                    mSelectedLevel = i;
                    mTargetScrollX = mSelectedLevel * 200.0f;
                }
                break;
            }
        }
    } else {
        // End of drag: Snap to the nearest level
        int nearestLevel = (int)std::round(mTargetScrollX / 200.0f);
        
        // Bound checks
        int playerMaxLevel = mApp->mPlayerInfo ? mApp->mPlayerInfo->mLevel : 1;
        int currentWorld = (playerMaxLevel - 1) / 10;
        int maxLevelInWorld = 10;
        if (mSelectedWorld == currentWorld) {
            maxLevelInWorld = (playerMaxLevel - 1) % 10 + 1;
        }

        if (nearestLevel < 1) nearestLevel = 1;
        if (nearestLevel > maxLevelInWorld) nearestLevel = maxLevelInWorld;

        mSelectedLevel = nearestLevel;
        mTargetScrollX = mSelectedLevel * 200.0f;
    }
}

void LevelSelector::KeyDown(KeyCode theKey) {
    bool debugHandled = false;
    
    // Save Manipulation (F9/F10) - KEPT as requested
    if (mApp->mPlayerInfo) {
        if (theKey == KeyCode::KEYCODE_F9) { 
            if (mApp->mPlayerInfo->mLevel > 1) {
                mApp->mPlayerInfo->mLevel--; 
                // Sync backup so it doesn't auto-restore to the old high level
                mApp->mPlayerInfo->mChallengeRecords[99] = (uint32_t)mApp->mPlayerInfo->mLevel;
                debugHandled = true; 
            }
        }
        if (theKey == KeyCode::KEYCODE_F10) { 
            if (mApp->mPlayerInfo->mLevel < 50) {
                mApp->mPlayerInfo->mLevel++; 
                // Sync backup
                mApp->mPlayerInfo->mChallengeRecords[99] = (uint32_t)mApp->mPlayerInfo->mLevel;
                debugHandled = true; 
            }
        }
    }

    if (debugHandled) {
        mApp->PlaySample(Sexy::SOUND_TAP);
        SyncButtons();
        MarkDirty();
    }

    if (theKey == KeyCode::KEYCODE_ESCAPE || theKey == KeyCode::KEYCODE_BACK) {
        ButtonDepress(LevelSelector_Back);
    }
}

void LevelSelector::ButtonDepress(int theId) {
    if (theId == LevelSelector_Back) {
        mApp->KillLevelSelector();
        mApp->ShowGameSelector();
    }
}

void LevelSelector::ButtonPress(int theId) {}
void LevelSelector::ButtonDownTick(int theId) {}

void LevelSelector::ButtonMouseEnter(int theId) {
    mApp->SetCursor(Sexy::CURSOR_HAND);
}

void LevelSelector::ButtonMouseLeave(int theId) {
    mApp->SetCursor(Sexy::CURSOR_POINTER);
}

void LevelSelector::ButtonMouseMove(int theId, int theX, int theY) {}
